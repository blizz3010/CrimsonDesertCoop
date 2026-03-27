#pragma once

#include <cstdint>
#include <array>

namespace cdcoop {

// These structures are reverse-engineered from the BlackSpace Engine.
// Offsets WILL need to be updated as the game patches.
// Use the companion tool (tools/offset_scanner.py) to find updated offsets.

// NOTE: All offsets below are PLACEHOLDERS. They must be discovered via
// reverse engineering (Cheat Engine, x64dbg, ReClass.NET, etc.)
// The structure layouts are educated guesses based on common game engine patterns
// and the existing CrimsonDesert-player-status-modifier research.

struct Vec3 {
    float x, y, z;

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    float length_sq() const { return dot(*this); }
};

struct Quat {
    float x, y, z, w;
};

struct Transform {
    Vec3 position;
    Quat rotation;
    Vec3 scale;
};

// Game's internal player structure (partial reconstruction)
// These offsets are starting points that need verification via RE tools
namespace offsets {
    namespace Player {
        // Base offsets from player pointer
        constexpr uint32_t POSITION       = 0x0;   // PLACEHOLDER - Vec3
        constexpr uint32_t ROTATION       = 0x0;   // PLACEHOLDER - Quat
        constexpr uint32_t HEALTH         = 0x0;   // PLACEHOLDER - float
        constexpr uint32_t MAX_HEALTH     = 0x0;   // PLACEHOLDER - float
        constexpr uint32_t STAMINA        = 0x0;   // PLACEHOLDER - float
        constexpr uint32_t ANIM_STATE     = 0x0;   // PLACEHOLDER - uint32_t
        constexpr uint32_t ANIM_BLEND     = 0x0;   // PLACEHOLDER - float
        constexpr uint32_t IS_ATTACKING   = 0x0;   // PLACEHOLDER - bool
        constexpr uint32_t IS_DODGING     = 0x0;   // PLACEHOLDER - bool
        constexpr uint32_t WEAPON_ID      = 0x0;   // PLACEHOLDER - uint32_t
        constexpr uint32_t MOVEMENT_SPEED = 0x0;   // PLACEHOLDER - float
        constexpr uint32_t GRAVITY_SCALE  = 0x0;   // PLACEHOLDER - float
    }

    namespace Companion {
        // Companion/NPC structure offsets
        constexpr uint32_t POSITION       = 0x0;   // PLACEHOLDER - Vec3
        constexpr uint32_t AI_CONTROLLER  = 0x0;   // PLACEHOLDER - ptr
        constexpr uint32_t MODEL_ID       = 0x0;   // PLACEHOLDER - uint32_t
        constexpr uint32_t ANIM_STATE     = 0x0;   // PLACEHOLDER - uint32_t
        constexpr uint32_t IS_ACTIVE      = 0x0;   // PLACEHOLDER - bool
    }

    namespace Enemy {
        constexpr uint32_t POSITION       = 0x0;   // PLACEHOLDER - Vec3
        constexpr uint32_t HEALTH         = 0x0;   // PLACEHOLDER - float
        constexpr uint32_t MAX_HEALTH     = 0x0;   // PLACEHOLDER - float
        constexpr uint32_t AGGRO_TARGET   = 0x0;   // PLACEHOLDER - ptr
        constexpr uint32_t STATE          = 0x0;   // PLACEHOLDER - uint32_t
    }

    namespace World {
        constexpr uint32_t GAME_INSTANCE  = 0x0;   // PLACEHOLDER - static ptr
        constexpr uint32_t PLAYER_PTR     = 0x0;   // PLACEHOLDER - offset from game instance
        constexpr uint32_t COMPANION_LIST = 0x0;   // PLACEHOLDER - offset to companion array
        constexpr uint32_t ENEMY_LIST     = 0x0;   // PLACEHOLDER - offset to enemy manager
        constexpr uint32_t CAMERA_MGR     = 0x0;   // PLACEHOLDER - offset to camera
    }
}

// Helper to read game memory safely
template<typename T>
T read_mem(uintptr_t base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

template<typename T>
void write_mem(uintptr_t base, uint32_t offset, const T& value) {
    *reinterpret_cast<T*>(base + offset) = value;
}

// Safe pointer chain dereference (follows pointer chain, returns 0 on null)
uintptr_t resolve_ptr_chain(uintptr_t base, std::initializer_list<uint32_t> offsets);

} // namespace cdcoop
