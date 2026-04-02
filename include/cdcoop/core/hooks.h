#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <safetyhook.hpp>

namespace cdcoop {

// Signature scan result
struct SigScanResult {
    uintptr_t address = 0;
    bool found = false;
    operator bool() const { return found; }
    operator uintptr_t() const { return address; }
};

// Central hook manager - discovers and hooks game functions
class HookManager {
public:
    static HookManager& instance();

    bool initialize();
    void shutdown();

    // Signature scanning in the game module
    SigScanResult sig_scan(const std::string& pattern, const std::string& name = "");

    // Hook a function by address
    template<typename T>
    bool create_hook(uintptr_t target, T detour, SafetyHookInline& hook) {
        hook = safetyhook::create_inline(reinterpret_cast<void*>(target),
                                          reinterpret_cast<void*>(detour));
        return hook.operator bool();
    }

    // Hook a function found via signature scan
    template<typename T>
    bool create_hook(const std::string& signature, const std::string& name,
                     T detour, SafetyHookInline& hook) {
        auto result = sig_scan(signature, name);
        if (!result) return false;
        return create_hook(result.address, detour, hook);
    }

    uintptr_t game_base() const { return game_base_; }
    size_t game_size() const { return game_size_; }

    // Resolve the WorldSystem singleton from signature scanning
    bool resolve_world_system();

    // Resolve the player actor from the WorldSystem chain
    bool resolve_player_actor();

private:
    HookManager() = default;

    uintptr_t game_base_ = 0;
    size_t game_size_ = 0;
    bool initialized_ = false;
};

// Game function hooks - these are the specific hooks we install
namespace hooks {

// Player position write hook
// Signature will need to be found via reverse engineering
// Placeholder pattern based on typical position update functions
inline SafetyHookInline player_position_hook;
void __cdecl player_position_detour(void* player, float x, float y, float z);

// Player animation state hook
inline SafetyHookInline player_animation_hook;
void __cdecl player_animation_detour(void* player, uint32_t anim_id, float blend);

// Damage calculation hook
inline SafetyHookInline damage_calc_hook;
float __cdecl damage_calc_detour(void* attacker, void* target, float base_damage);

// Companion spawn hook - we intercept this to spawn player 2
inline SafetyHookInline companion_spawn_hook;
void* __cdecl companion_spawn_detour(void* spawn_params);

// World state change hook (doors, chests, quest triggers)
inline SafetyHookInline world_state_hook;
void __cdecl world_state_detour(void* world_obj, uint32_t state_id, uint32_t new_state);

// Camera system hook (for split-screen or tethered camera)
inline SafetyHookInline camera_hook;
void __cdecl camera_detour(void* camera, void* target_transform);

// Game tick / update hook - main sync point
inline SafetyHookInline game_tick_hook;
void __cdecl game_tick_detour(void* game_instance, float delta_time);

} // namespace hooks

} // namespace cdcoop
