#include <cdcoop/sync/enemy_sync.h>
#include <cdcoop/network/session.h>
#include <cdcoop/core/config.h>
#include <cdcoop/core/game_structures.h>
#include <cdcoop/player/companion_hijack.h>
#include <spdlog/spdlog.h>

namespace cdcoop {

EnemySync& EnemySync::instance() {
    static EnemySync inst;
    return inst;
}

void EnemySync::initialize() {
    auto& session = Session::instance();

    session.register_handler(PacketType::ENEMY_STATE,
        [this](PacketType, const uint8_t* data, size_t size) {
            on_remote_enemy_state(data, size);
        });

    session.register_handler(PacketType::ENEMY_DAMAGE,
        [this](PacketType, const uint8_t* data, size_t size) {
            on_remote_enemy_damage(data, size);
        });

    session.register_handler(PacketType::ENEMY_DEATH,
        [this](PacketType, const uint8_t* data, size_t size) {
            on_remote_enemy_death(data, size);
        });

    spdlog::info("EnemySync initialized");
}

void EnemySync::shutdown() {
    revert_coop_scaling();
}

void EnemySync::update(float delta_time) {
    auto& session = Session::instance();
    if (!session.is_active() || !session.is_host()) return;

    // Host broadcasts enemy states at a fixed rate
    send_timer_ += delta_time;
    if (send_timer_ < ENEMY_SYNC_RATE) return;
    send_timer_ = 0.0f;

    // Iterate actors from the ActorManager to find enemies and broadcast state.
    // The ActorManager holds all entities; we identify enemies by checking
    // that they are not the player or a hijacked companion.
    auto& rt = get_runtime_offsets();
    if (!is_valid_ptr(rt.actor_manager_ptr)) return;

    uintptr_t player = rt.player_actor_ptr;
    uintptr_t companion = CompanionHijack::instance().get_entity_ptr();

    // Scan body slots for enemy entities (same slot structure as companion scan)
    constexpr int MAX_BODY_SLOTS = 8;
    constexpr uint32_t BODY_SLOT_BASE = ActorStructure::BODY_SLOT_0;

    for (int i = 0; i < MAX_BODY_SLOTS; i++) {
        uint32_t slot_offset = BODY_SLOT_BASE + static_cast<uint32_t>(i * 8);
        uintptr_t entity = read_mem<uintptr_t>(rt.actor_manager_ptr, slot_offset);
        if (!is_valid_ptr(entity)) continue;
        if (entity == player || entity == companion) continue;
        // Skip ChildActor instances (companions, summons). When the
        // vtable hasn't been resolved yet, is_child_actor returns false
        // and we fall back to the pointer-equality check above.
        if (is_child_actor(entity)) continue;

        // Read enemy position via the verified pointer chain
        Vec3 pos = {0, 0, 0};
        uintptr_t core = resolve_ptr_chain(entity, {
            offsets::Player::ACTOR_TO_INNER,
            offsets::Player::INNER_TO_CORE
        });
        if (is_valid_ptr(core)) {
            uintptr_t pos_struct = resolve_ptr_chain(core, {
                offsets::Player::POS_OWNER_TO_STRUCT
            });
            if (is_valid_ptr(pos_struct)) {
                pos = {
                    read_mem<float>(pos_struct, offsets::Player::POS_STRUCT_X),
                    read_mem<float>(pos_struct, offsets::Player::POS_STRUCT_Y),
                    read_mem<float>(pos_struct, offsets::Player::POS_STRUCT_Z)
                };
            }
        }
        uint32_t state = read_mem<uint32_t>(entity, offsets::Enemy::STATE);

        // Read enemy health via the stat entry system (same as player)
        // Stats component is a pointer at actor+0x58, dereference to get base
        float health = 0.0f;
        uintptr_t stat_base = resolve_ptr_chain(entity, {offsets::Player::STAT_COMPONENT});
        if (is_valid_ptr(stat_base)) {
            int64_t raw_hp = read_mem<int64_t>(stat_base, StatEntry::CURRENT_VALUE);
            health = static_cast<float>(raw_hp) / 1000.0f;
        }

        uint32_t entity_id = static_cast<uint32_t>(entity & 0xFFFFFFFF);
        on_enemy_state_changed(entity_id, pos, {0,0,0,1}, health, state);
    }
}

void EnemySync::on_enemy_state_changed(uint32_t entity_id, const Vec3& pos,
                                        const Quat& rot, float health, uint32_t state) {
    if (!Session::instance().is_host()) return;

    EnemyStatePacket pkt{};
    pkt.header.type = PacketType::ENEMY_STATE;
    pkt.header.payload_size = sizeof(pkt) - sizeof(PacketHeader);
    pkt.entity_id = entity_id;
    pkt.position = pos;
    pkt.rotation = rot;
    pkt.health = health;
    pkt.state = state;

    Session::instance().send_packet(pkt, false); // Unreliable, high frequency
}

void EnemySync::on_enemy_death(uint32_t entity_id) {
    EnemyStatePacket pkt{};
    pkt.header.type = PacketType::ENEMY_DEATH;
    pkt.header.payload_size = sizeof(pkt) - sizeof(PacketHeader);
    pkt.entity_id = entity_id;
    pkt.health = 0;

    Session::instance().send_packet(pkt, true); // Reliable - deaths are important
}

void EnemySync::on_enemy_spawn(uint32_t entity_id, const Vec3& pos, uint32_t type_id) {
    // Only host sends spawn notifications
    if (!Session::instance().is_host()) return;

    EnemyStatePacket pkt{};
    pkt.header.type = PacketType::ENEMY_SPAWN;
    pkt.header.payload_size = sizeof(pkt) - sizeof(PacketHeader);
    pkt.entity_id = entity_id;
    pkt.position = pos;
    pkt.state = type_id;

    Session::instance().send_packet(pkt, true);
}

void EnemySync::report_damage(uint32_t entity_id, float damage) {
    // Called from the damage_calc_detour every time the player hits an
    // enemy, including when nobody is in a session. Skip the allocation
    // + serialise + transport-null-check work in that case so we don't
    // burn cycles on every swing during single-player play.
    auto& session = Session::instance();
    if (!session.is_active()) return;

    EnemyDamagePacket pkt{};
    pkt.header.type = PacketType::ENEMY_DAMAGE;
    pkt.header.payload_size = sizeof(pkt) - sizeof(PacketHeader);
    pkt.entity_id = entity_id;
    pkt.damage = damage;
    pkt.damage_source = session.is_host() ? 0 : 1;

    session.send_packet(pkt, true);
}

void EnemySync::apply_coop_scaling() {
    auto& cfg = get_config();
    auto& rt = get_runtime_offsets();

    spdlog::info("Applying co-op scaling: HP x{:.1f}, DMG x{:.1f}",
                  cfg.enemy_hp_multiplier, cfg.enemy_dmg_multiplier);

    if (!is_valid_ptr(rt.actor_manager_ptr)) {
        spdlog::warn("EnemySync: cannot apply scaling - ActorManager not available");
        return;
    }

    uintptr_t player = rt.player_actor_ptr;
    uintptr_t companion = CompanionHijack::instance().get_entity_ptr();

    // Scan body slots for enemy entities and scale their HP
    constexpr int MAX_BODY_SLOTS = 8;
    constexpr uint32_t BODY_SLOT_BASE = ActorStructure::BODY_SLOT_0;
    int scaled_count = 0;

    for (int i = 0; i < MAX_BODY_SLOTS; i++) {
        uint32_t slot_offset = BODY_SLOT_BASE + static_cast<uint32_t>(i * 8);
        uintptr_t entity = read_mem<uintptr_t>(rt.actor_manager_ptr, slot_offset);
        if (!is_valid_ptr(entity)) continue;
        if (entity == player || entity == companion) continue;
        // Skip ChildActor instances (companions, summons). When the
        // vtable hasn't been resolved yet, is_child_actor returns false
        // and we fall back to the pointer-equality check above.
        if (is_child_actor(entity)) continue;

        uint32_t entity_id = static_cast<uint32_t>(entity & 0xFFFFFFFF);

        // Dereference the stats component pointer and scale max health
        uintptr_t stat_base = resolve_ptr_chain(entity, {offsets::Player::STAT_COMPONENT});
        if (is_valid_ptr(stat_base)) {
            int64_t max_hp = read_mem<int64_t>(stat_base, StatEntry::MAX_VALUE);
            if (max_hp > 0) {
                // Save original value
                original_stats_[entity_id] = { static_cast<float>(max_hp) };

                // Scale up
                int64_t scaled_hp = static_cast<int64_t>(max_hp * cfg.enemy_hp_multiplier);
                write_mem<int64_t>(stat_base, StatEntry::MAX_VALUE, scaled_hp);
                write_mem<int64_t>(stat_base, StatEntry::CURRENT_VALUE, scaled_hp);
                scaled_count++;
            }
        }
    }

    spdlog::info("Co-op scaling applied to {} enemies", scaled_count);
}

void EnemySync::revert_coop_scaling() {
    if (original_stats_.empty()) return;

    auto& rt = get_runtime_offsets();
    spdlog::info("Reverting co-op scaling for {} enemies", original_stats_.size());

    if (is_valid_ptr(rt.actor_manager_ptr)) {
        constexpr int MAX_BODY_SLOTS = 8;
        constexpr uint32_t BODY_SLOT_BASE = ActorStructure::BODY_SLOT_0;

        for (int i = 0; i < MAX_BODY_SLOTS; i++) {
            uint32_t slot_offset = BODY_SLOT_BASE + static_cast<uint32_t>(i * 8);
            uintptr_t entity = read_mem<uintptr_t>(rt.actor_manager_ptr, slot_offset);
            if (!is_valid_ptr(entity)) continue;

            uint32_t entity_id = static_cast<uint32_t>(entity & 0xFFFFFFFF);
            auto it = original_stats_.find(entity_id);
            if (it != original_stats_.end()) {
                uintptr_t stat_base = resolve_ptr_chain(entity, {offsets::Player::STAT_COMPONENT});
                if (is_valid_ptr(stat_base)) {
                    int64_t original_hp = static_cast<int64_t>(it->second.max_health);
                    write_mem<int64_t>(stat_base, StatEntry::MAX_VALUE, original_hp);
                }
            }
        }
    }

    original_stats_.clear();
}

// --- Private ---

void EnemySync::on_remote_enemy_state(const uint8_t* data, size_t size) {
    if (size < sizeof(EnemyStatePacket)) return;
    auto* pkt = reinterpret_cast<const EnemyStatePacket*>(data);

    // Client receives authoritative enemy state from host.
    // Find the local enemy entity by scanning body slots and matching entity_id.
    auto& rt = get_runtime_offsets();
    if (!is_valid_ptr(rt.actor_manager_ptr)) return;

    constexpr int MAX_BODY_SLOTS = 8;
    constexpr uint32_t BODY_SLOT_BASE = ActorStructure::BODY_SLOT_0;

    for (int i = 0; i < MAX_BODY_SLOTS; i++) {
        uint32_t slot_offset = BODY_SLOT_BASE + static_cast<uint32_t>(i * 8);
        uintptr_t entity = read_mem<uintptr_t>(rt.actor_manager_ptr, slot_offset);
        if (!is_valid_ptr(entity)) continue;

        uint32_t eid = static_cast<uint32_t>(entity & 0xFFFFFFFF);
        if (eid != pkt->entity_id) continue;

        // Apply position from host
        uintptr_t core = resolve_ptr_chain(entity, {
            offsets::Player::ACTOR_TO_INNER,
            offsets::Player::INNER_TO_CORE
        });
        if (is_valid_ptr(core)) {
            uintptr_t pos_struct = resolve_ptr_chain(core, {
                offsets::Player::POS_OWNER_TO_STRUCT
            });
            if (is_valid_ptr(pos_struct)) {
                write_mem<float>(pos_struct, offsets::Player::POS_STRUCT_X, pkt->position.x);
                write_mem<float>(pos_struct, offsets::Player::POS_STRUCT_Y, pkt->position.y);
                write_mem<float>(pos_struct, offsets::Player::POS_STRUCT_Z, pkt->position.z);
            }
        }

        // Apply state
        write_mem<uint32_t>(entity, offsets::Enemy::STATE, pkt->state);
        break;
    }
}

void EnemySync::on_remote_enemy_damage(const uint8_t* data, size_t size) {
    if (size < sizeof(EnemyDamagePacket)) return;

    // Host receives damage report from client and validates
    if (!Session::instance().is_host()) return;

    auto* pkt = reinterpret_cast<const EnemyDamagePacket*>(data);
    spdlog::debug("Remote damage: enemy {} took {:.1f} damage", pkt->entity_id, pkt->damage);

    // Validate damage is reasonable (anti-cheat: cap at 10x normal)
    float max_reasonable_damage = 50000.0f;
    float validated_damage = (pkt->damage > max_reasonable_damage) ? max_reasonable_damage : pkt->damage;

    // Find the enemy and apply damage by reducing current HP
    auto& rt = get_runtime_offsets();
    if (!is_valid_ptr(rt.actor_manager_ptr)) return;

    constexpr int MAX_BODY_SLOTS = 8;
    constexpr uint32_t BODY_SLOT_BASE = ActorStructure::BODY_SLOT_0;

    for (int i = 0; i < MAX_BODY_SLOTS; i++) {
        uint32_t slot_offset = BODY_SLOT_BASE + static_cast<uint32_t>(i * 8);
        uintptr_t entity = read_mem<uintptr_t>(rt.actor_manager_ptr, slot_offset);
        if (!is_valid_ptr(entity)) continue;

        uint32_t eid = static_cast<uint32_t>(entity & 0xFFFFFFFF);
        if (eid != pkt->entity_id) continue;

        // Dereference stats component and apply damage
        uintptr_t stat_base = resolve_ptr_chain(entity, {offsets::Player::STAT_COMPONENT});
        if (!is_valid_ptr(stat_base)) break;

        int64_t current_hp = read_mem<int64_t>(stat_base, StatEntry::CURRENT_VALUE);
        int64_t damage_scaled = static_cast<int64_t>(validated_damage * 1000.0f);
        int64_t new_hp = current_hp - damage_scaled;

        if (new_hp <= 0) {
            new_hp = 0;
            on_enemy_death(pkt->entity_id);
        }

        write_mem<int64_t>(stat_base, StatEntry::CURRENT_VALUE, new_hp);
        break;
    }
}

void EnemySync::on_remote_enemy_death(const uint8_t* data, size_t size) {
    if (size < sizeof(EnemyStatePacket)) return;

    auto* pkt = reinterpret_cast<const EnemyStatePacket*>(data);
    spdlog::info("Enemy {} died", pkt->entity_id);

    // Set enemy HP to 0 and state to dead
    auto& rt = get_runtime_offsets();
    if (!is_valid_ptr(rt.actor_manager_ptr)) return;

    constexpr int MAX_BODY_SLOTS = 8;
    constexpr uint32_t BODY_SLOT_BASE = ActorStructure::BODY_SLOT_0;

    for (int i = 0; i < MAX_BODY_SLOTS; i++) {
        uint32_t slot_offset = BODY_SLOT_BASE + static_cast<uint32_t>(i * 8);
        uintptr_t entity = read_mem<uintptr_t>(rt.actor_manager_ptr, slot_offset);
        if (!is_valid_ptr(entity)) continue;

        uint32_t eid = static_cast<uint32_t>(entity & 0xFFFFFFFF);
        if (eid != pkt->entity_id) continue;

        // Zero out HP via dereferenced stats component
        uintptr_t stat_base = resolve_ptr_chain(entity, {offsets::Player::STAT_COMPONENT});
        if (is_valid_ptr(stat_base)) {
            write_mem<int64_t>(stat_base, StatEntry::CURRENT_VALUE, 0);
        }
        break;
    }
}

} // namespace cdcoop
