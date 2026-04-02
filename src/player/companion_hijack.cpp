#include <cdcoop/player/companion_hijack.h>
#include <cdcoop/core/hooks.h>
#include <cdcoop/core/game_structures.h>
#include <spdlog/spdlog.h>

namespace cdcoop {

CompanionHijack& CompanionHijack::instance() {
    static CompanionHijack inst;
    return inst;
}

bool CompanionHijack::initialize() {
    // Find the companion system/manager in the game's memory
    // This is the system that manages AI companions like Oongka, Yann, Naira
    //
    // Approach to finding this:
    // 1. Use ReClass.NET or Cheat Engine to find companion entities
    // 2. Trace back to the manager/spawner that owns them
    // 3. Find the companion list/array
    //
    // The companion system likely lives as a subsystem of the game instance:
    //   GameInstance -> CompanionManager -> CompanionEntity[]

    auto& rt = get_runtime_offsets();

    // Resolve companion system from the WorldSystem/ActorManager chain.
    // Companions are actors managed by the ActorManager, reachable via the
    // same WorldSystem singleton used for the player actor.
    if (rt.world_system_resolved && is_valid_ptr(rt.actor_manager_ptr)) {
        companion_system_ = rt.actor_manager_ptr;
    } else if (rt.world_system_resolved) {
        companion_system_ = resolve_ptr_chain(rt.world_system_ptr, {
            offsets::World::ACTOR_MANAGER
        });
    }

    if (companion_system_ == 0) {
        spdlog::warn("CompanionHijack: could not find companion system");
        spdlog::warn("This is expected if no companion is currently spawned");
        return false;
    }

    spdlog::info("CompanionHijack: companion system at 0x{:X}", companion_system_);
    return true;
}

void CompanionHijack::shutdown() {
    deactivate();
}

bool CompanionHijack::activate() {
    if (active_) {
        spdlog::warn("CompanionHijack: already active");
        return true;
    }

    if (companion_system_ == 0) {
        // Try to re-initialize
        if (!initialize()) return false;
    }

    // Strategy: find the first active companion and take it over
    //
    // Crimson Desert has companion characters (Oongka, Yann, Naira) that
    // fight alongside the player. We replace one's AI controller with our
    // network-driven controller.
    //
    // Steps:
    // 1. Read companion array from companion_system_
    // 2. Find first active companion
    // 3. Save its AI controller pointer
    // 4. Null out / replace the AI controller (disables AI)
    // 5. We now control this entity via set_position/set_animation

    // Iterate companion/NPC actor body slots from the ActorManager.
    // The player actor uses body slots at offsets 0xD0-0x108 (8 slots, 8 bytes each)
    // which hold pointers to child actors including companions.
    // We scan these slots on the player actor to find companion entities.
    constexpr int MAX_BODY_SLOTS = 8;
    constexpr uint32_t BODY_SLOT_BASE = ActorStructure::BODY_SLOT_0; // 0xD0

    uintptr_t player_actor = get_runtime_offsets().player_actor_ptr;
    if (!is_valid_ptr(player_actor)) {
        spdlog::warn("CompanionHijack: player actor not available for companion scan");
    } else {
        for (int i = 0; i < MAX_BODY_SLOTS; i++) {
            uint32_t slot_offset = BODY_SLOT_BASE + static_cast<uint32_t>(i * 8);
            uintptr_t companion = read_mem<uintptr_t>(player_actor, slot_offset);
            if (!is_valid_ptr(companion)) continue;

            // Skip if this is the player actor itself
            if (companion == player_actor) continue;

            // Check if the entity has an AI controller (companions do, player doesn't)
            uintptr_t ai_ctrl = read_mem<uintptr_t>(companion, offsets::Companion::AI_CONTROLLER);
            if (!is_valid_ptr(ai_ctrl)) continue;

            hijacked_entity_ = companion;
            hijacked_slot_ = i;
            original_ai_ctrl_ = ai_ctrl;
            disable_ai();
            active_ = true;
            spdlog::info("CompanionHijack: found companion at body slot {} (0x{:X})",
                          i, companion);
            break;
        }
    }

    if (!active_) {
        spdlog::error("CompanionHijack: no active companion found to hijack");
        return false;
    }

    spdlog::info("CompanionHijack: hijacked companion slot {} (entity: 0x{:X})",
                  hijacked_slot_, hijacked_entity_);
    return true;
}

void CompanionHijack::deactivate() {
    if (!active_) return;

    // Restore AI controller
    enable_ai();

    spdlog::info("CompanionHijack: released companion slot {}", hijacked_slot_);

    hijacked_entity_ = 0;
    original_ai_ctrl_ = 0;
    hijacked_slot_ = -1;
    active_ = false;
}

void CompanionHijack::set_position(const Vec3& pos, const Quat& rot) {
    if (!active_ || !is_valid_ptr(hijacked_entity_)) return;

    // Use the verified position chain: actor -> +0x40 -> +0x08 -> core -> +0x248 -> +0x90
    // This writes to the authoritative position, not a cache.
    uintptr_t core = resolve_ptr_chain(hijacked_entity_, {
        offsets::Player::ACTOR_TO_INNER,
        offsets::Player::INNER_TO_CORE
    });

    if (is_valid_ptr(core)) {
        uintptr_t pos_struct = resolve_ptr_chain(core, {
            offsets::Player::POS_OWNER_TO_STRUCT
        });
        if (is_valid_ptr(pos_struct)) {
            write_mem<float>(pos_struct, offsets::Player::POS_STRUCT_X, pos.x);
            write_mem<float>(pos_struct, offsets::Player::POS_STRUCT_Y, pos.y);
            write_mem<float>(pos_struct, offsets::Player::POS_STRUCT_Z, pos.z);
            // Rotation: write to the next float4 slot after position (+0xA0)
            write_mem<Quat>(pos_struct, offsets::Player::POS_STRUCT_X + 0x10, rot);
            return;
        }
    }

    // Fallback: direct write to entity base (less reliable)
    write_mem<float>(hijacked_entity_, offsets::Player::POSITION_X, pos.x);
    write_mem<float>(hijacked_entity_, offsets::Player::POSITION_Y, pos.y);
    write_mem<float>(hijacked_entity_, offsets::Player::POSITION_Z, pos.z);
}

void CompanionHijack::set_animation(uint32_t anim_id, float blend, float speed, float time) {
    if (!active_ || !is_valid_ptr(hijacked_entity_)) return;

    // Write animation state to the companion entity.
    // Companions share the same actor layout as the player.
    write_mem<uint32_t>(hijacked_entity_, offsets::Companion::ANIM_STATE, anim_id);
    write_mem<float>(hijacked_entity_, offsets::Player::ANIM_BLEND, blend);
}

void CompanionHijack::set_health(float health, float max_health) {
    if (!active_ || hijacked_entity_ == 0) return;

    // Sync player 2's health display
    // This is mainly cosmetic - the actual HP is tracked by the remote player's game
}

void CompanionHijack::disable_ai() {
    if (!is_valid_ptr(hijacked_entity_)) return;

    // Null out the AI controller pointer so the companion doesn't move on its own.
    // The AI controller is at offset 0x48 (component link) on the actor.
    write_mem<uintptr_t>(hijacked_entity_, offsets::Companion::AI_CONTROLLER, 0);
    spdlog::debug("CompanionHijack: AI disabled for entity 0x{:X}", hijacked_entity_);
}

void CompanionHijack::enable_ai() {
    if (!is_valid_ptr(hijacked_entity_) || original_ai_ctrl_ == 0) return;

    // Restore the original AI controller
    write_mem<uintptr_t>(hijacked_entity_, offsets::Companion::AI_CONTROLLER, original_ai_ctrl_);
    spdlog::debug("CompanionHijack: AI restored for entity 0x{:X}", hijacked_entity_);
}

} // namespace cdcoop
