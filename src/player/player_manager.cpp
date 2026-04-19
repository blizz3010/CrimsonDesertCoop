#include <cdcoop/player/player_manager.h>
#include <cdcoop/core/hooks.h>
#include <cdcoop/core/game_structures.h>
#include <cdcoop/player/companion_hijack.h>
#include <spdlog/spdlog.h>

namespace cdcoop {

PlayerManager& PlayerManager::instance() {
    static PlayerManager inst;
    return inst;
}

bool PlayerManager::initialize() {
    find_local_player();

    if (local_player_ == 0) {
        spdlog::error("PlayerManager: could not find local player");
        spdlog::error("Make sure you are in-game (not in a menu) before the mod initializes");
        return false;
    }

    spdlog::info("PlayerManager: local player at 0x{:X}", local_player_);
    return true;
}

void PlayerManager::shutdown() {
    despawn_remote_player();
    local_player_ = 0;
    game_instance_ = 0;
}

void PlayerManager::update([[maybe_unused]] float delta_time) {
    // Re-acquire player pointer if lost (e.g., after loading screen)
    if (local_player_ == 0) {
        find_local_player();
    }
}

Vec3 PlayerManager::local_position() const {
    if (local_player_ == 0) return {0, 0, 0};

    // Verified authoritative position chain (from position_research.md):
    //   actor -> +0x40 -> +0x08 -> player_core (-> +0x248 -> pos_struct -> +0x90)
    // Try the verified chain first, fall back to direct position hook pointer.
    uintptr_t player_core = resolve_ptr_chain(local_player_, {
        offsets::Player::ACTOR_TO_INNER,
        offsets::Player::INNER_TO_CORE
    });

    if (is_valid_ptr(player_core)) {
        uintptr_t pos_struct = resolve_ptr_chain(player_core, {
            offsets::Player::POS_OWNER_TO_STRUCT
        });
        if (is_valid_ptr(pos_struct)) {
            return {
                read_mem<float>(pos_struct, offsets::Player::POS_STRUCT_X),
                read_mem<float>(pos_struct, offsets::Player::POS_STRUCT_Y),
                read_mem<float>(pos_struct, offsets::Player::POS_STRUCT_Z)
            };
        }
    }

    // Fallback: read directly from actor base (from position hook r13)
    return {
        read_mem<float>(local_player_, offsets::Player::POSITION_X),
        read_mem<float>(local_player_, offsets::Player::POSITION_Y),
        read_mem<float>(local_player_, offsets::Player::POSITION_Z)
    };
}

Quat PlayerManager::local_rotation() const {
    if (local_player_ == 0) return {0, 0, 0, 1};
    // Rotation follows the position float4 (x,y,z,w) at +0x90.
    // The next float4 at +0xA0 is likely rotation (needs verification).
    uintptr_t player_core = resolve_ptr_chain(local_player_, {
        offsets::Player::ACTOR_TO_INNER,
        offsets::Player::INNER_TO_CORE
    });
    if (is_valid_ptr(player_core)) {
        uintptr_t pos_struct = resolve_ptr_chain(player_core, {
            offsets::Player::POS_OWNER_TO_STRUCT
        });
        if (is_valid_ptr(pos_struct)) {
            return read_mem<Quat>(pos_struct, offsets::Player::ROTATION_QUAT);
        }
    }
    return {0, 0, 0, 1};
}

float PlayerManager::local_health() const {
    if (local_player_ == 0) return 0;
    auto& rt = get_runtime_offsets();
    if (rt.player_stats_component != 0) {
        int64_t raw = read_mem<int64_t>(rt.player_stats_component, StatEntry::CURRENT_VALUE);
        return static_cast<float>(raw) / 1000.0f;
    }
    return 0;
}

float PlayerManager::local_max_health() const {
    if (local_player_ == 0) return 0;
    auto& rt = get_runtime_offsets();
    if (rt.player_stats_component != 0) {
        int64_t raw = read_mem<int64_t>(rt.player_stats_component, StatEntry::MAX_VALUE);
        return static_cast<float>(raw) / 1000.0f;
    }
    return 0;
}

uintptr_t PlayerManager::remote_player() const {
    return CompanionHijack::instance().get_entity_ptr();
}

void PlayerManager::spawn_remote_player() {
    spdlog::info("Spawning remote player (via companion hijack)...");

    auto& hijack = CompanionHijack::instance();
    if (!hijack.is_active()) {
        if (!hijack.activate()) {
            spdlog::error("Failed to spawn remote player - no companion to hijack");
            spdlog::info("Tip: Make sure a companion (Oongka/Yann/Naira) is in your party");
            return;
        }
    }

    spdlog::info("Remote player spawned successfully");
}

void PlayerManager::despawn_remote_player() {
    CompanionHijack::instance().deactivate();
    spdlog::info("Remote player despawned");
}

bool PlayerManager::is_remote_spawned() const {
    return CompanionHijack::instance().is_active();
}

void PlayerManager::find_local_player() {
    // Player finding strategy uses the WorldSystem chain discovered by
    // CrimsonDesertTools/EquipHide:
    //   WorldSystem -> ActorManager (+0x30) -> UserActor (+0x28)
    //
    // The WorldSystem singleton is resolved via RIP-relative sig scan
    // in HookManager::resolve_world_system().
    //
    // Additionally, the PlayerPointerCapture hook (from player-status-modifier)
    // gives us the player pointer at runtime via rax register.

    auto& rt = get_runtime_offsets();

    // Method 1: Use the pre-resolved player actor from WorldSystem chain
    if (rt.player_resolved && is_valid_ptr(rt.player_actor_ptr)) {
        local_player_ = rt.player_actor_ptr;
        game_instance_ = rt.world_system_ptr;
        spdlog::info("PlayerManager: found player via WorldSystem chain at 0x{:X}", local_player_);
        return;
    }

    // Method 2: Try resolving the chain ourselves if WorldSystem is known
    if (rt.world_system_resolved && is_valid_ptr(rt.world_system_ptr)) {
        auto& hooks = HookManager::instance();
        if (hooks.resolve_player_actor()) {
            local_player_ = rt.player_actor_ptr;
            game_instance_ = rt.world_system_ptr;
            spdlog::info("PlayerManager: resolved player at 0x{:X}", local_player_);
            return;
        }
    }

    // Method 3: Try the PlayerPointerCapture signature directly
    // This scans for the function that accesses the player pointer
    auto& hooks = HookManager::instance();
    auto result = hooks.sig_scan(signatures::PLAYER_PTR_PRIMARY, "PlayerPointerCapture");
    if (!result) {
        result = hooks.sig_scan(signatures::PLAYER_PTR_FALLBACK, "PlayerPointerCapture_FB");
    }

    if (result) {
        spdlog::info("PlayerManager: found PlayerPointerCapture function at 0x{:X}", result.address);
        spdlog::info("PlayerManager: player pointer will be captured at runtime via hook");
        // The actual pointer capture happens in the hook callback
    } else {
        spdlog::warn("PlayerManager: all player finding methods failed");
        spdlog::warn("PlayerManager: player pointer will be unavailable until resolved");
    }
}

} // namespace cdcoop
