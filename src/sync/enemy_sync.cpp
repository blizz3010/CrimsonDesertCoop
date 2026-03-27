#include <cdcoop/sync/enemy_sync.h>
#include <cdcoop/network/session.h>
#include <cdcoop/core/config.h>
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

    // TODO: iterate the game's enemy list and broadcast state for each
    // This requires:
    // 1. Finding the enemy manager pointer (offsets::World::ENEMY_LIST)
    // 2. Iterating the enemy array
    // 3. For each enemy, read position/health/state and send to client
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
    // Client reports damage to host for validation
    EnemyDamagePacket pkt{};
    pkt.header.type = PacketType::ENEMY_DAMAGE;
    pkt.header.payload_size = sizeof(pkt) - sizeof(PacketHeader);
    pkt.entity_id = entity_id;
    pkt.damage = damage;
    pkt.damage_source = Session::instance().is_host() ? 0 : 1;

    Session::instance().send_packet(pkt, true);
}

void EnemySync::apply_coop_scaling() {
    auto& cfg = get_config();
    spdlog::info("Applying co-op scaling: HP x{:.1f}, DMG x{:.1f}",
                  cfg.enemy_hp_multiplier, cfg.enemy_dmg_multiplier);

    // TODO: iterate all loaded enemies and scale their max HP
    // Save original values in original_stats_ for revert
    // This requires finding the enemy list in memory
}

void EnemySync::revert_coop_scaling() {
    if (original_stats_.empty()) return;

    spdlog::info("Reverting co-op scaling for {} enemies", original_stats_.size());

    // TODO: restore original HP values from original_stats_
    original_stats_.clear();
}

// --- Private ---

void EnemySync::on_remote_enemy_state(const uint8_t* data, size_t size) {
    if (size < sizeof(EnemyStatePacket)) return;

    // Client receives authoritative enemy state from host
    // Apply position/health/state to the local enemy entity
    // auto* pkt = reinterpret_cast<const EnemyStatePacket*>(data);
    // TODO: find local enemy by entity_id and update its state
}

void EnemySync::on_remote_enemy_damage(const uint8_t* data, size_t size) {
    if (size < sizeof(EnemyDamagePacket)) return;

    // Host receives damage report from client
    if (!Session::instance().is_host()) return;

    auto* pkt = reinterpret_cast<const EnemyDamagePacket*>(data);
    spdlog::debug("Remote damage: enemy {} took {:.1f} damage", pkt->entity_id, pkt->damage);

    // TODO: apply damage to the enemy entity
    // This is where the host validates that the damage is reasonable
}

void EnemySync::on_remote_enemy_death(const uint8_t* data, size_t size) {
    if (size < sizeof(EnemyStatePacket)) return;

    auto* pkt = reinterpret_cast<const EnemyStatePacket*>(data);
    spdlog::info("Enemy {} died", pkt->entity_id);

    // TODO: trigger death on the local enemy entity
}

} // namespace cdcoop
