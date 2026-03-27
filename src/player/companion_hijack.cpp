#include <cdcoop/player/companion_hijack.h>
#include <cdcoop/core/hooks.h>
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

    auto& hooks = HookManager::instance();

    // TODO: use sig scan to find companion manager
    // companion_system_ = resolve_ptr_chain(hooks.game_base(), {
    //     offsets::World::GAME_INSTANCE,
    //     offsets::World::COMPANION_LIST
    // });

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

    // TODO: iterate companion slots
    // for (int i = 0; i < MAX_COMPANIONS; i++) {
    //     uintptr_t companion = read_mem<uintptr_t>(companion_system_, i * 8);
    //     if (companion == 0) continue;
    //     bool is_active = read_mem<bool>(companion, offsets::Companion::IS_ACTIVE);
    //     if (!is_active) continue;
    //
    //     hijacked_entity_ = companion;
    //     hijacked_slot_ = i;
    //     original_ai_ctrl_ = read_mem<uintptr_t>(companion, offsets::Companion::AI_CONTROLLER);
    //     disable_ai();
    //     active_ = true;
    //     break;
    // }

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
    if (!active_ || hijacked_entity_ == 0) return;

    // Write position directly to the companion entity
    // write_mem<Vec3>(hijacked_entity_, offsets::Companion::POSITION, pos);
    // TODO: write rotation as well
}

void CompanionHijack::set_animation(uint32_t anim_id, float blend, float speed, float time) {
    if (!active_ || hijacked_entity_ == 0) return;

    // Write animation state to the companion's animation controller
    // write_mem<uint32_t>(hijacked_entity_, offsets::Companion::ANIM_STATE, anim_id);
}

void CompanionHijack::set_health(float health, float max_health) {
    if (!active_ || hijacked_entity_ == 0) return;

    // Sync player 2's health display
    // This is mainly cosmetic - the actual HP is tracked by the remote player's game
}

void CompanionHijack::disable_ai() {
    if (hijacked_entity_ == 0) return;

    // Null out the AI controller pointer so the companion doesn't move on its own
    // write_mem<uintptr_t>(hijacked_entity_, offsets::Companion::AI_CONTROLLER, 0);
    spdlog::debug("CompanionHijack: AI disabled for entity 0x{:X}", hijacked_entity_);
}

void CompanionHijack::enable_ai() {
    if (hijacked_entity_ == 0 || original_ai_ctrl_ == 0) return;

    // Restore the original AI controller
    // write_mem<uintptr_t>(hijacked_entity_, offsets::Companion::AI_CONTROLLER, original_ai_ctrl_);
    spdlog::debug("CompanionHijack: AI restored for entity 0x{:X}", hijacked_entity_);
}

} // namespace cdcoop
