#include <cdcoop/core/hooks.h>
#include <cdcoop/core/memory.h>
#include <cdcoop/core/game_structures.h>
#include <cdcoop/network/session.h>
#include <cdcoop/sync/player_sync.h>
#include <cdcoop/sync/enemy_sync.h>
#include <cdcoop/sync/world_sync.h>
#include <cdcoop/player/companion_hijack.h>
#include <cdcoop/ui/overlay.h>

#include <spdlog/spdlog.h>
#include <Windows.h>

namespace cdcoop {

HookManager& HookManager::instance() {
    static HookManager inst;
    return inst;
}

bool HookManager::initialize() {
    if (initialized_) return true;

    // Find the main game module (CrimsonDesert.exe or similar)
    HMODULE game_module = GetModuleHandleA(nullptr);
    if (!game_module) {
        spdlog::error("Failed to get game module handle");
        return false;
    }

    game_base_ = reinterpret_cast<uintptr_t>(game_module);

    // Get module size from PE header
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(game_base_);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(game_base_ + dos->e_lfanew);
    game_size_ = nt->OptionalHeader.SizeOfImage;

    spdlog::info("Game module: base=0x{:X}, size=0x{:X}", game_base_, game_size_);

    // =====================================================================
    // HOOK INSTALLATION
    // =====================================================================
    // These signatures are PLACEHOLDERS and must be discovered via reverse
    // engineering. Use tools like:
    //   - x64dbg + pattern finder
    //   - Cheat Engine + AOB scan
    //   - IDA Pro / Ghidra
    //   - ReClass.NET (for structure discovery)
    //
    // The existing CrimsonDesert-player-status-modifier project demonstrates
    // that mid-function hooking works in this game. Use similar techniques
    // to find the functions listed below.
    // =====================================================================

    // TODO: Replace these placeholder signatures with real ones from RE
    // The format is IDA-style: "48 89 5C 24 ? 57 48 83 EC 20"

    // Game tick / main update loop
    // This is our primary sync point - called every frame
    // Look for: the main game loop update function
    // create_hook("PLACEHOLDER_GAME_TICK_SIG", "GameTick",
    //             hooks::game_tick_detour, hooks::game_tick_hook);

    // Player position update
    // Look for: function that writes to player X/Y/Z coordinates
    // create_hook("PLACEHOLDER_PLAYER_POS_SIG", "PlayerPosition",
    //             hooks::player_position_detour, hooks::player_position_hook);

    // Companion spawn function
    // Look for: function that creates companion entities
    // create_hook("PLACEHOLDER_COMPANION_SPAWN_SIG", "CompanionSpawn",
    //             hooks::companion_spawn_detour, hooks::companion_spawn_hook);

    // Damage calculation
    // Look for: function that computes final damage value
    // create_hook("PLACEHOLDER_DAMAGE_CALC_SIG", "DamageCalc",
    //             hooks::damage_calc_detour, hooks::damage_calc_hook);

    spdlog::warn("Using PLACEHOLDER hooks - signatures must be filled in via RE!");
    spdlog::warn("See docs/REVERSE_ENGINEERING.md for instructions");

    initialized_ = true;
    return true;
}

void HookManager::shutdown() {
    if (!initialized_) return;

    // SafetyHook automatically restores original code when hooks are destroyed
    hooks::game_tick_hook = {};
    hooks::player_position_hook = {};
    hooks::player_animation_hook = {};
    hooks::damage_calc_hook = {};
    hooks::companion_spawn_hook = {};
    hooks::world_state_hook = {};
    hooks::camera_hook = {};

    initialized_ = false;
    spdlog::info("All hooks removed");
}

SigScanResult HookManager::sig_scan(const std::string& pattern, const std::string& name) {
    auto addr = MemoryScanner::scan_module(pattern);
    if (addr) {
        spdlog::info("Found '{}' at 0x{:X} (RVA: 0x{:X})", name, addr, addr - game_base_);
    } else {
        spdlog::error("Failed to find '{}'", name);
    }
    return {addr, addr != 0};
}

// =========================================================================
// Hook detour implementations
// =========================================================================

namespace hooks {

void __cdecl game_tick_detour(void* game_instance, float delta_time) {
    // Call original first
    game_tick_hook.call<void>(game_instance, delta_time);

    // Then run our co-op update logic
    auto& session = Session::instance();
    if (session.is_active()) {
        session.update(delta_time);
        PlayerSync::instance().update(delta_time);
        EnemySync::instance().update(delta_time);
        WorldSync::instance().update(delta_time);
    }

    // Update overlay regardless of session state
    Overlay::instance().render();

    // Check hotkeys
    static bool f7_was_pressed = false;
    bool f7_pressed = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
    if (f7_pressed && !f7_was_pressed) {
        if (session.state() == SessionState::DISCONNECTED) {
            session.host_session();
        }
    }
    f7_was_pressed = f7_pressed;

    static bool f8_was_pressed = false;
    bool f8_pressed = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (f8_pressed && !f8_was_pressed) {
        Overlay::instance().toggle_visible();
    }
    f8_was_pressed = f8_pressed;
}

void __cdecl player_position_detour(void* player, float x, float y, float z) {
    // Call original
    player_position_hook.call<void>(player, x, y, z);

    // Broadcast our position to peer
    if (Session::instance().is_active()) {
        Vec3 pos{x, y, z};
        // TODO: read rotation and velocity from player struct
        Quat rot{0, 0, 0, 1};
        Vec3 vel{0, 0, 0};
        PlayerSync::instance().on_local_position_changed(pos, rot, vel, 0);
    }
}

void __cdecl player_animation_detour(void* player, uint32_t anim_id, float blend) {
    player_animation_hook.call<void>(player, anim_id, blend);

    if (Session::instance().is_active()) {
        PlayerSync::instance().on_local_animation_changed(anim_id, blend, 1.0f, 0.0f);
    }
}

float __cdecl damage_calc_detour(void* attacker, void* target, float base_damage) {
    float damage = damage_calc_hook.call<float>(attacker, target, base_damage);

    if (Session::instance().is_active()) {
        // If client hit an enemy, report it to host
        // If we're host, the damage is applied locally
        // TODO: identify attacker/target entity IDs
    }

    return damage;
}

void* __cdecl companion_spawn_detour(void* spawn_params) {
    void* companion = companion_spawn_hook.call<void*>(spawn_params);

    // If we're in a co-op session and need to hijack a companion slot
    if (Session::instance().is_active() && companion) {
        auto& hijack = CompanionHijack::instance();
        if (!hijack.is_active()) {
            spdlog::info("Companion spawned during co-op - attempting hijack");
            hijack.activate();
        }
    }

    return companion;
}

void __cdecl world_state_detour(void* world_obj, uint32_t state_id, uint32_t new_state) {
    world_state_hook.call<void>(world_obj, state_id, new_state);

    if (Session::instance().is_active()) {
        // Forward world state changes to peer
        // TODO: extract object_id from world_obj
        WorldSync::instance().on_world_interact(0, state_id, new_state);
    }
}

void __cdecl camera_detour(void* camera, void* target_transform) {
    camera_hook.call<void>(camera, target_transform);

    // In co-op, we may want to adjust the camera to keep both players in view
    // or implement a tethered camera system
    // TODO: implement camera adjustments for co-op
}

} // namespace hooks

} // namespace cdcoop
