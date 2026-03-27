#include <cdcoop/player/player_manager.h>
#include <cdcoop/core/hooks.h>
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

void PlayerManager::update(float delta_time) {
    // Re-acquire player pointer if lost (e.g., after loading screen)
    if (local_player_ == 0) {
        find_local_player();
    }
}

Vec3 PlayerManager::local_position() const {
    if (local_player_ == 0) return {0, 0, 0};
    return read_mem<Vec3>(local_player_, offsets::Player::POSITION);
}

Quat PlayerManager::local_rotation() const {
    if (local_player_ == 0) return {0, 0, 0, 1};
    return read_mem<Quat>(local_player_, offsets::Player::ROTATION);
}

float PlayerManager::local_health() const {
    if (local_player_ == 0) return 0;
    return read_mem<float>(local_player_, offsets::Player::HEALTH);
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
    // Strategy to find the player entity:
    //
    // Method 1: Signature scan for the player singleton access function
    // Many games have a static function like GetPlayerCharacter() that
    // returns the player pointer. Find this via signature scanning.
    //
    // Method 2: Pointer chain from game instance
    // GameInstance (static) -> World -> PlayerController -> PlayerCharacter
    //
    // Method 3: The existing CrimsonDesert-player-status-modifier project
    // already found player stat write functions. The first argument to those
    // functions is typically the player pointer. Use the same signatures
    // and read the pointer from the function's context.
    //
    // For now, we use Method 2 as a starting point:

    auto& hooks = HookManager::instance();

    // TODO: implement actual player finding
    // game_instance_ = resolve_ptr_chain(hooks.game_base(), {
    //     offsets::World::GAME_INSTANCE
    // });
    //
    // if (game_instance_) {
    //     local_player_ = resolve_ptr_chain(game_instance_, {
    //         offsets::World::PLAYER_PTR
    //     });
    // }

    // PLACEHOLDER: This will be 0 until proper offsets are discovered
    spdlog::debug("PlayerManager: searching for player pointer...");
}

} // namespace cdcoop
