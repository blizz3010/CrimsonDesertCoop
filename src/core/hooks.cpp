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
    // Signatures sourced from:
    //   - CrimsonDesert-player-status-modifier (Orcax-1399)
    //   - CrimsonDesertTools/EquipHide (tkhquang)
    // Game version: v1.00.02 / v1.01.03 (March 2026)
    // =====================================================================

    int hooks_installed = 0;
    int hooks_failed = 0;

    // --- WorldSystem singleton resolution (from EquipHide) ---
    // This gives us access to the actor manager and player actor
    resolve_world_system();

    // --- Player position hook ---
    // From player-status-modifier: hooks the position height access function
    // At hook point: r13 = float* position array [X, Y, Z]
    if (create_hook(signatures::POSITION_PRIMARY, "PositionAccess",
                    hooks::player_position_detour, hooks::player_position_hook)) {
        spdlog::info("Installed PositionAccess hook (primary sig)");
        hooks_installed++;
    } else if (create_hook(signatures::POSITION_FALLBACK, "PositionAccess",
                           hooks::player_position_detour, hooks::player_position_hook)) {
        spdlog::info("Installed PositionAccess hook (fallback sig)");
        hooks_installed++;
    } else {
        spdlog::error("Failed to install PositionAccess hook");
        hooks_failed++;
    }

    // --- Damage calculation hook ---
    // From player-status-modifier: hooks the damage slot access
    // At hook point: r15 = damage source ptr, r12 = damage amount (32-bit)
    if (create_hook(signatures::DAMAGE_SLOT_PRIMARY, "DamageSlot",
                    hooks::damage_calc_detour, hooks::damage_calc_hook)) {
        spdlog::info("Installed DamageSlot hook");
        hooks_installed++;
    } else {
        spdlog::error("Failed to install DamageSlot hook");
        hooks_failed++;
    }

    // --- Stat write hook ---
    // From player-status-modifier: intercepts the shared stat write path
    // Health, stamina, and spirit all go through this opcode
    if (create_hook(signatures::STAT_WRITE_PRIMARY, "StatWrite",
                    hooks::world_state_detour, hooks::world_state_hook)) {
        spdlog::info("Installed StatWrite hook (primary sig)");
        hooks_installed++;
    } else if (create_hook(signatures::STAT_WRITE_FALLBACK, "StatWrite",
                           hooks::world_state_detour, hooks::world_state_hook)) {
        spdlog::info("Installed StatWrite hook (fallback sig)");
        hooks_installed++;
    } else {
        spdlog::error("Failed to install StatWrite hook");
        hooks_failed++;
    }

    spdlog::info("Hook installation complete: {} installed, {} failed", hooks_installed, hooks_failed);
    if (hooks_failed > 0) {
        spdlog::warn("Some hooks failed - game version may have changed.");
        spdlog::warn("Try updating signatures. See docs/REVERSE_ENGINEERING.md");
    }

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

bool HookManager::resolve_world_system() {
    auto& rt = get_runtime_offsets();

    // Try WorldSystem signature patterns (from CrimsonDesertTools EquipHide)
    // These use RIP-relative addressing to find the WorldSystem singleton

    auto try_rip_resolve = [&](const char* sig, const char* name,
                                int rip_offset, int rip_end) -> bool {
        auto result = sig_scan(sig, name);
        if (!result) return false;

        // Follow the RIP-relative address
        uintptr_t rip_addr = result.address + rip_offset;
        rt.world_system_ptr = MemoryScanner::follow_rel32(rip_addr, 0);

        if (is_valid_ptr(rt.world_system_ptr)) {
            // Dereference to get actual WorldSystem pointer
            uintptr_t ws = *reinterpret_cast<uintptr_t*>(rt.world_system_ptr);
            if (is_valid_ptr(ws)) {
                rt.world_system_ptr = ws;
                rt.world_system_resolved = true;
                spdlog::info("WorldSystem resolved at 0x{:X}", rt.world_system_ptr);
                return resolve_player_actor();
            }
        }
        spdlog::warn("WorldSystem pointer invalid from {}", name);
        return false;
    };

    if (try_rip_resolve(signatures::WORLD_SYSTEM_P1, "WorldSystem_P1",
                        signatures::WORLD_SYSTEM_P1_RIP_OFFSET,
                        signatures::WORLD_SYSTEM_P1_RIP_END))
        return true;

    if (try_rip_resolve(signatures::WORLD_SYSTEM_P2, "WorldSystem_P2",
                        signatures::WORLD_SYSTEM_P2_RIP_OFFSET,
                        signatures::WORLD_SYSTEM_P2_RIP_END))
        return true;

    if (try_rip_resolve(signatures::WORLD_SYSTEM_P3, "WorldSystem_P3",
                        signatures::WORLD_SYSTEM_P3_RIP_OFFSET,
                        signatures::WORLD_SYSTEM_P3_RIP_END))
        return true;

    spdlog::error("Failed to resolve WorldSystem - all signature patterns failed");
    return false;
}

bool HookManager::resolve_player_actor() {
    auto& rt = get_runtime_offsets();
    if (!rt.world_system_resolved) return false;

    // Follow the chain: WorldSystem -> ActorManager -> UserActor
    uintptr_t actor_mgr = resolve_ptr_chain(rt.world_system_ptr, {
        offsets::World::ACTOR_MANAGER
    });

    if (!is_valid_ptr(actor_mgr)) {
        spdlog::warn("ActorManager not found (world_system=0x{:X})", rt.world_system_ptr);
        return false;
    }

    rt.actor_manager_ptr = actor_mgr;

    uintptr_t player = resolve_ptr_chain(actor_mgr, {
        offsets::World::USER_ACTOR
    });

    if (!is_valid_ptr(player)) {
        spdlog::warn("Player actor not found (actor_mgr=0x{:X})", actor_mgr);
        return false;
    }

    rt.player_actor_ptr = player;
    rt.player_resolved = true;
    spdlog::info("Player actor resolved at 0x{:X}", player);
    return true;
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

    // Update runtime position pointer.
    // At this hook point (from PositionHeightAccess sig), r13 = float* position.
    // The function arguments give us the position components directly.
    auto& rt = get_runtime_offsets();
    rt.position_resolved = true;

    // Broadcast our position to peer
    if (Session::instance().is_active()) {
        Vec3 pos{x, y, z};
        // Read rotation from the player actor if available
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
        auto& rt = get_runtime_offsets();
        auto attacker_addr = reinterpret_cast<uintptr_t>(attacker);
        auto target_addr = reinterpret_cast<uintptr_t>(target);

        // Check if the attacker is the remote player (companion entity)
        auto remote_entity = CompanionHijack::instance().get_entity_ptr();
        bool from_remote = (remote_entity != 0 && attacker_addr == remote_entity);

        if (from_remote || attacker_addr == rt.player_actor_ptr) {
            // Report damage to EnemySync for co-op damage tracking
            // Use the target pointer as a simple entity ID
            uint32_t target_id = static_cast<uint32_t>(target_addr & 0xFFFFFFFF);
            EnemySync::instance().report_damage(target_id, damage);
        }
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
        // Extract object ID from the world object pointer
        uint32_t object_id = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(world_obj) & 0xFFFFFFFF);
        WorldSync::instance().on_world_interact(object_id, state_id, new_state);
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
