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

    // --- Player base pointer resolution (from bbfox0703 CT, v1.01.03) ---
    // Additional fallback: resolve the player static base via RIP-relative sig
    if (!get_runtime_offsets().player_resolved) {
        resolve_player_base();
    }

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

    // --- Game tick: DX12 Present hook drives the update loop ---
    // No dedicated game tick signature exists for the BlackSpace Engine.
    // The DX12 Present hook (installed by Overlay in imgui_impl_dx12.cpp) fires
    // once per frame and calls sync systems (Session, PlayerSync, EnemySync,
    // WorldSync, PlayerManager) directly with accurate delta_time.
    spdlog::info("Game tick: using DX12 Present hook as frame tick (no dedicated sig needed)");

    // --- Hooks NOT yet installed (awaiting verified signatures) ---
    // player_animation_hook: Animation state offsets 0x120/0x124 are estimated,
    //   not verified. No animation write signature found yet. Animation sync
    //   currently relies on 5Hz full-state fallback packets.
    // companion_spawn_hook: No companion spawn function signature has been found.
    //   Companion hijack currently scans body slots at session start instead.

    // --- Camera Zoom/FOV hook (from Send's CE table, game v1.00.03) ---
    // Hooks the instruction: movss [r12+0xD8], xmm0
    // r12 = camera struct base, 0xD8 = zoom/FOV offset
    // This lets us capture the camera struct pointer and modify FOV for co-op
    if (create_hook(signatures::CAMERA_ZOOM_FOV, "CameraZoomFOV",
                    hooks::camera_detour, hooks::camera_hook)) {
        spdlog::info("Installed CameraZoomFOV hook (full sig)");
        hooks_installed++;
    } else if (create_hook(signatures::CAMERA_ZOOM_FOV_NONWILD, "CameraZoomFOV",
                           hooks::camera_detour, hooks::camera_hook)) {
        spdlog::info("Installed CameraZoomFOV hook (non-wildcard sig)");
        hooks_installed++;
    } else {
        spdlog::warn("Failed to install CameraZoomFOV hook - co-op camera tether disabled");
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

    // Resolve the stats component (pointer at actor + 0x58)
    uintptr_t stat_base = resolve_ptr_chain(player, {offsets::Player::STAT_COMPONENT});
    if (is_valid_ptr(stat_base)) {
        rt.player_stats_component = stat_base;
        spdlog::info("Player stats component at 0x{:X}", stat_base);
    } else {
        spdlog::warn("Player stats component not found");
    }

    return true;
}

bool HookManager::resolve_player_base() {
    auto& rt = get_runtime_offsets();

    // Method 1: Signature-based RIP-relative resolution (from bbfox0703 CT)
    auto result = sig_scan(signatures::PLAYER_BASE_DISCOVERY, "PlayerBaseDiscovery");
    if (result) {
        uintptr_t rip_addr = result.address + signatures::PLAYER_BASE_DISCOVERY_RIP_OFFSET;
        uintptr_t player_base_storage = MemoryScanner::follow_rel32(rip_addr, 0);

        if (is_valid_ptr(player_base_storage)) {
            uintptr_t player_base = *reinterpret_cast<uintptr_t*>(player_base_storage);
            if (is_valid_ptr(player_base)) {
                // Follow the chain to find the player actor:
                // base -> +0x18 -> +0xA0 -> +0xD0 -> +0x68 (Kliff)
                uintptr_t actor = resolve_ptr_chain(player_base, {
                    offsets::PlayerBase::CHAIN_0,
                    offsets::PlayerBase::CHAIN_1,
                    offsets::PlayerBase::CHAIN_2,
                    offsets::PlayerBase::SLOT_KLIFF
                });

                if (is_valid_ptr(actor)) {
                    rt.player_actor_ptr = actor;
                    rt.player_resolved = true;
                    // Also resolve stats component
                    uintptr_t sb = resolve_ptr_chain(actor, {offsets::Player::STAT_COMPONENT});
                    if (is_valid_ptr(sb)) rt.player_stats_component = sb;
                    spdlog::info("Player resolved via PlayerBaseDiscovery sig at 0x{:X}", actor);
                    return true;
                }
            }
        }
    }

    // Method 2: Hardcoded static base (last resort, v1.01.03)
    uintptr_t static_base = game_base_ + offsets::PlayerBase::STATIC_RVA;
    uintptr_t player_base = *reinterpret_cast<uintptr_t*>(static_base);
    if (is_valid_ptr(player_base)) {
        uintptr_t actor = resolve_ptr_chain(player_base, {
            offsets::PlayerBase::CHAIN_0,
            offsets::PlayerBase::CHAIN_1,
            offsets::PlayerBase::CHAIN_2,
            offsets::PlayerBase::SLOT_KLIFF
        });

        if (is_valid_ptr(actor)) {
            rt.player_actor_ptr = actor;
            rt.player_resolved = true;
            // Also resolve stats component
            uintptr_t sb = resolve_ptr_chain(actor, {offsets::Player::STAT_COMPONENT});
            if (is_valid_ptr(sb)) rt.player_stats_component = sb;
            spdlog::info("Player resolved via static base (0x{:X}) at 0x{:X}",
                         offsets::PlayerBase::STATIC_RVA, actor);
            return true;
        }
    }

    spdlog::warn("Failed to resolve player via PlayerBase methods");
    return false;
}

// =========================================================================
// Hook detour implementations
// =========================================================================

namespace hooks {

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
        // Read rotation from the player actor via the verified pointer chain
        Quat rot{0, 0, 0, 1};
        auto& rt = get_runtime_offsets();
        if (is_valid_ptr(rt.player_actor_ptr)) {
            uintptr_t player_core = resolve_ptr_chain(rt.player_actor_ptr, {
                offsets::Player::ACTOR_TO_INNER, offsets::Player::INNER_TO_CORE
            });
            if (is_valid_ptr(player_core)) {
                uintptr_t pos_struct = resolve_ptr_chain(player_core, {
                    offsets::Player::POS_OWNER_TO_STRUCT
                });
                if (is_valid_ptr(pos_struct)) {
                    rot = read_mem<Quat>(pos_struct, offsets::Player::ROTATION_QUAT);
                }
            }
        }
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

void __cdecl camera_detour(void* camera_struct, void* unused) {
    camera_hook.call<void>(camera_struct, unused);

    // Camera Zoom/FOV hook (from Send's CE table, game v1.00.03)
    // At hook point: r12 = camera struct base, offset 0xD8 = zoom/FOV float
    // The original instruction: movss [r12+0xD8], xmm0
    //
    // We capture the camera struct pointer for co-op use and can adjust
    // the zoom/FOV to pull the camera back when players are far apart.

    auto camera_addr = reinterpret_cast<uintptr_t>(camera_struct);
    if (!is_valid_ptr(camera_addr)) return;

    // Capture camera struct pointer for other systems to use
    auto& rt = get_runtime_offsets();
    rt.camera_struct_ptr = camera_addr;
    rt.camera_resolved = true;

    if (!Session::instance().is_active()) return;

    auto& ps = PlayerSync::instance();
    if (!ps.is_tether_active()) return;

    // When players are far apart, increase zoom distance to keep both in view
    auto local_pos = PlayerManager::instance().local_position();
    auto remote_pos = ps.remote_state().position;

    Vec3 diff = local_pos - remote_pos;
    float dist_sq = diff.length_sq();

    // Read current zoom/FOV from camera struct + 0xD8
    float current_fov = read_mem<float>(camera_addr, offsets::Camera::ZOOM_FOV);

    // If players are more than 15m apart, start pulling camera back
    constexpr float TETHER_START_SQ = 15.0f * 15.0f;   // 225
    constexpr float TETHER_MAX_SQ   = 40.0f * 40.0f;   // 1600
    constexpr float BASE_ZOOM       = 8.0f;             // Default max zoom out
    constexpr float MAX_ZOOM         = 14.0f;            // Extended zoom for co-op

    if (dist_sq > TETHER_START_SQ) {
        // Lerp zoom from BASE_ZOOM to MAX_ZOOM based on player distance
        float t = (dist_sq - TETHER_START_SQ) / (TETHER_MAX_SQ - TETHER_START_SQ);
        if (t > 1.0f) t = 1.0f;
        float target_zoom = BASE_ZOOM + (MAX_ZOOM - BASE_ZOOM) * t;

        // Only override if we'd pull further out than the game's current value
        if (target_zoom > current_fov) {
            write_mem<float>(camera_addr, offsets::Camera::ZOOM_FOV, target_zoom);
        }
    }
}

} // namespace hooks

} // namespace cdcoop
