#pragma once

#include <cstdint>
#include <array>
#include <string>

namespace cdcoop {

// These structures are reverse-engineered from the BlackSpace Engine.
// Offsets WILL need to be updated as the game patches.
// Use the companion tool (tools/offset_scanner.py) to find updated offsets.
//
// Sources for offsets:
//   - CrimsonDesert-player-status-modifier (Orcax-1399) - player stats, position, damage
//   - CrimsonDesertTools/EquipHide (tkhquang) - WorldSystem, actor structure, body navigation
//   - CrimsonDesertModdingResearch (marvelmaster) - address value table
//   - FearLess Cheat Engine community - stat entry structure
//
// Game version: v1.00.02 / v1.01.03 (March 2026)

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

// =========================================================================
// Stat entry structure discovered by the community:
//   +0x00  int32_t  stat_type  (0=Health, 17=Stamina, 18=Spirit)
//   +0x08  int64_t  current_value  (displayed value * 1000, 8-byte)
//   +0x18  int64_t  max_value
// All three stats share the same opcode for writes.
// =========================================================================
namespace StatEntry {
    constexpr uint32_t TYPE           = 0x00;   // int32_t stat identifier
    constexpr uint32_t CURRENT_VALUE  = 0x08;   // int64_t (actual = displayed * 1000)
    constexpr uint32_t MAX_VALUE      = 0x18;   // int64_t (actual = displayed * 1000)

    // Stat type identifiers
    constexpr int32_t HEALTH_ID       = 0;
    constexpr int32_t STAMINA_ID      = 17;
    constexpr int32_t SPIRIT_ID       = 18;
}

// Durability entry structure (from CrimsonDesert-player-status-modifier)
namespace DurabilityEntry {
    constexpr uint32_t CURRENT        = 0x50;   // uint16_t current durability
    constexpr uint32_t DELTA          = 0x38;   // int16_t durability delta
    constexpr uint32_t ABYSS_CURRENT  = 0x02;   // uint16_t (abyss variant)
}

// =========================================================================
// Actor / Entity structure (from CrimsonDesertTools EquipHide RE work)
// =========================================================================
namespace ActorStructure {
    constexpr uint32_t VTABLE         = 0x00;   // ptr - vtable pointer
    constexpr uint32_t COMPONENT_LINK = 0x48;   // ptr - component linkage
    constexpr uint32_t D8_FIELD       = 0xD8;   // ptr - fallback mode field

    // Body offsets (player candidate pointers within actor)
    constexpr uint32_t BODY_SLOT_0    = 0xD0;
    constexpr uint32_t BODY_SLOT_1    = 0xD8;
    constexpr uint32_t BODY_SLOT_2    = 0xE0;
    constexpr uint32_t BODY_SLOT_3    = 0xE8;
    constexpr uint32_t BODY_SLOT_4    = 0xF0;
    constexpr uint32_t BODY_SLOT_5    = 0xF8;
    constexpr uint32_t BODY_SLOT_6    = 0x100;
    constexpr uint32_t BODY_SLOT_7    = 0x108;

    // Body -> VisCtrl chain
    constexpr uint32_t BODY_TO_VIS_1  = 0x68;
    constexpr uint32_t BODY_TO_VIS_2  = 0x40;
    constexpr uint32_t BODY_TO_VIS_3  = 0xE8;
    constexpr uint32_t VIS_TO_COMP    = 0x48;
    constexpr uint32_t COMP_TO_DESC   = 0x218;
    constexpr uint32_t DESC_MAP_BASE  = 0x20;
}

// =========================================================================
// PartInOutSocket structure (equipment visibility, from EquipHide)
// =========================================================================
namespace PartInOutSocket {
    constexpr uint32_t VISIBLE_BYTE   = 0x1C;   // 0=both, 1=in-only, 2=out-only, 3=skip
}

// =========================================================================
// Player offsets - resolved dynamically via signature scanning
// The player pointer is obtained from the PlayerPointerCapture hook,
// where rax = actor pointer, rsi = status marker pointer.
//
// Verified pointer chains (from position_research.md):
//   Actor -> +0x40 -> +0x08 -> player_core
//   position_owner -> +0x248 -> position_struct -> +0x90 (x,y,z,w float4)
//   Status marker: [rdx+0x68] -> [rax+0x20]
//
// Position write instruction at CrimsonDesert.exe+36ADB8C:
//   41 0F 11 45 00 (movups [r13+00], xmm0)
//   r13 points directly at the authoritative position vector
//
// Candidate static base pointers (unstable, use sig scanning instead):
//   CrimsonDesert.exe+05EDB400
//   CrimsonDesert.exe+05C008A0
// =========================================================================
namespace offsets {
    namespace Player {
        // These are resolved at runtime from the player actor.
        // The stat entry is found by scanning the stat component (base+0x58 array).
        constexpr uint32_t STAT_COMPONENT = 0x58;  // Array of stat entries (from StatsAccess sig)

        // Verified actor -> player_core chain (from position_research.md):
        constexpr uint32_t ACTOR_TO_INNER = 0x40;  // First deref from actor base
        constexpr uint32_t INNER_TO_CORE  = 0x08;  // Second deref to player_core

        // Verified authoritative position (from position_research.md):
        // Reached via: position_owner -> +0x248 -> position_struct -> +0x90
        constexpr uint32_t POS_OWNER_TO_STRUCT = 0x248; // Deref to position struct
        constexpr uint32_t POS_STRUCT_X   = 0x90;  // float - X axis (verified)
        constexpr uint32_t POS_STRUCT_Y   = 0x94;  // float - Y axis (verified)
        constexpr uint32_t POS_STRUCT_Z   = 0x98;  // float - Z axis (verified)
        constexpr uint32_t POS_STRUCT_W   = 0x9C;  // float - W/padding (verified)

        // Position also accessible at hook time via r13 pointer:
        // r13 = float* pointing directly at the position vector
        constexpr uint32_t POSITION_X     = 0x00;  // float (xmm0.f32[0]) - at r13
        constexpr uint32_t POSITION_Y     = 0x04;  // float (xmm0.f32[1]) - height, at r13
        constexpr uint32_t POSITION_Z     = 0x08;  // float (xmm0.f32[2]) - at r13

        // Position cache (NOT authoritative, may lag behind):
        // player_core -> +0xA0 -> cache_block
        constexpr uint32_t POS_CACHE      = 0xA0;  // Deref to position cache block

        // Status marker chain: [rdx+0x68] -> [rax+0x20]
        constexpr uint32_t STATUS_MARKER_1 = 0x68;  // From rdx context
        constexpr uint32_t STATUS_MARKER_2 = 0x20;  // Second deref to marker

        // Position write RVA (relative to CrimsonDesert.exe base):
        // Instruction at +0x36ADB8C: 41 0F 11 45 00 (movups [r13+00], xmm0)
        constexpr uint32_t POS_WRITE_RVA  = 0x36ADB8C;

        // These offsets are from the actor base, discovered via EquipHide RE work.
        // They need fine-tuning with Cheat Engine / ReClass but are reasonable estimates
        // based on the BlackSpace Engine actor layout.
        constexpr uint32_t ANIM_STATE     = 0x120;  // uint32_t - animation state ID
        constexpr uint32_t ANIM_BLEND     = 0x124;  // float - blend weight
        constexpr uint32_t IS_ATTACKING   = 0x130;  // bool
        constexpr uint32_t IS_DODGING     = 0x131;  // bool
        constexpr uint32_t WEAPON_ID      = 0x134;  // uint32_t
        constexpr uint32_t MOVEMENT_SPEED = 0x140;  // float
        constexpr uint32_t GRAVITY_SCALE  = 0x144;  // float
    }

    namespace Companion {
        // Companion entities use the same actor base structure.
        // AI controller and active flag offsets from EquipHide body navigation.
        constexpr uint32_t AI_CONTROLLER  = 0x48;   // ptr - AI behavior controller (component link)
        constexpr uint32_t MODEL_ID       = 0x110;  // uint32_t
        constexpr uint32_t ANIM_STATE     = 0x120;  // uint32_t - same layout as player
        constexpr uint32_t IS_ACTIVE      = 0x1C;   // bool (PartInOutSocket visible byte)
    }

    namespace Enemy {
        // Enemies use the same actor base. Stats use the same StatEntry format.
        constexpr uint32_t AGGRO_TARGET   = 0x150;  // ptr - current aggro target actor
        constexpr uint32_t STATE          = 0x158;  // uint32_t - AI state enum
    }

    namespace World {
        // WorldSystem resolution chain (from EquipHide CrimsonDesertTools)
        // WorldSystem is a singleton found via RIP-relative pointer in WS_P1/P2/P3 sigs.
        constexpr uint32_t WORLD_SYSTEM   = 0x00;   // ptr - WorldSystem singleton (from sig scan)
        constexpr uint32_t ACTOR_MANAGER  = 0x30;   // ptr - ActorManager within WorldSystem
        constexpr uint32_t USER_ACTOR     = 0x28;   // ptr - user/player actor within ActorManager
        constexpr uint32_t WORLD_SUB_50   = 0x50;   // ptr - sub-object used in WS_P1 sig chain

        // For stat access: the stats component is at actor + 0x58, containing
        // an array of entries each 0x10 in size (shifted left 4 = * 16).
    }
}

// =========================================================================
// AOB Signatures - discovered from existing open-source mods
//
// From CrimsonDesert-player-status-modifier (Orcax-1399):
//   Player pointer, stats, position, damage, durability hooks
//
// From CrimsonDesertTools/EquipHide (tkhquang):
//   WorldSystem, actor vtable, map lookup, equipment visibility hooks
//
// Game version: v1.00.02 / v1.01.03 (March 2026)
// These are IDA-style patterns where '?' = wildcard byte
// =========================================================================
namespace signatures {

    // --- Player Pointer Capture (from player-status-modifier) ---
    // Hook: rax = actor ptr, rsi = status marker ptr, rdx = context
    constexpr const char* PLAYER_PTR_PRIMARY =
        "49 8B 7D 18 49 8B 44 24 40 48 8B 40 68 48 8B 70 20";
    constexpr int PLAYER_PTR_PRIMARY_OFFSET = 17;

    constexpr const char* PLAYER_PTR_FALLBACK =
        "49 8B 44 24 40 48 8B 40 68 48 8B 70 20";
    constexpr int PLAYER_PTR_FALLBACK_OFFSET = 13;

    // --- Stats Access (stat entry array iteration) ---
    // The stat component is at base+0x58, entries are 16 bytes each (C1 E0 04 = shl rax,4)
    constexpr const char* STATS_ACCESS_PRIMARY =
        "48 8D ? ? 48 C1 E0 04 48 03 46 58 ? 8B ? 24";
    constexpr int STATS_ACCESS_PRIMARY_OFFSET = 12;

    constexpr const char* STATS_ACCESS_FALLBACK =
        "48 C1 E0 04 48 03 46 58";
    constexpr int STATS_ACCESS_FALLBACK_OFFSET = 8;

    // --- Stat Write (the main stat write path, shared by health/stamina/spirit) ---
    constexpr const char* STAT_WRITE_PRIMARY =
        "48 2B 47 18 48 39 5F 18 48 0F 4F C2 48 89 47 20 48 FF 47 48 48 89 5F 08 48 8B 5C 24 48 48 89 77 38 66 89 6F 50";
    constexpr int STAT_WRITE_PRIMARY_OFFSET = 20;

    constexpr const char* STAT_WRITE_FALLBACK =
        "48 FF 47 48 48 89 5F 08 48 8B 5C 24 48 48 89 77 38";
    constexpr int STAT_WRITE_FALLBACK_OFFSET = 4;

    // --- Position Height Access (player position read/write) ---
    // Hook: r13 = float* position array [X, Y, Z]
    // xmm0 contains position components at hook time
    constexpr const char* POSITION_PRIMARY =
        "49 3B F7 0F 8C ? ? ? ? 0F 28 C6 F3 45 0F 5C C8 41 0F 58 45 00 41 0F 11 45 00 48 8B BB F8 00 00 00 48 63 83 00 01 00 00";
    constexpr int POSITION_PRIMARY_OFFSET = 22;

    constexpr const char* POSITION_FALLBACK =
        "0F 28 C6 F3 45 0F 5C C8 41 0F 58 45 00 41 0F 11 45 00";
    constexpr int POSITION_FALLBACK_OFFSET = 13;

    // --- Damage Slot Access ---
    // Hook: r15 = damage source ptr, r12 = damage amount (32-bit)
    constexpr const char* DAMAGE_SLOT_PRIMARY =
        "49 8B 77 38 44 8B 24 88 48 8D 4C 24 ? 4A 8B 1C E3";
    constexpr int DAMAGE_SLOT_PRIMARY_OFFSET = 4;

    // --- Damage Value Access ---
    // Same pattern, different hook offset for the damage value
    constexpr const char* DAMAGE_VALUE_PRIMARY =
        "49 8B 77 38 44 8B 24 88 48 8D 4C 24 ? 4A 8B 1C E3";
    constexpr int DAMAGE_VALUE_PRIMARY_OFFSET = 17;

    // --- Item Gain Access ---
    constexpr const char* ITEM_GAIN_PRIMARY =
        "49 01 4C 38 10";
    constexpr int ITEM_GAIN_PRIMARY_OFFSET = 0;

    // --- WorldSystem Resolution (from EquipHide) ---
    // Resolves the WorldSystem singleton via RIP-relative addressing
    constexpr const char* WORLD_SYSTEM_P1 =
        "48 83 EC 28 48 8B 0D ? ? ? ? 48 8B 49 50 E8 ? ? ? ? 84 C0 0F 94 C0 48 83 C4 28 C3";
    constexpr int WORLD_SYSTEM_P1_RIP_OFFSET = 7;
    constexpr int WORLD_SYSTEM_P1_RIP_END = 11;

    constexpr const char* WORLD_SYSTEM_P2 =
        "80 B8 49 01 00 00 00 75 ? 48 8B 05 ? ? ? ? 48 8B 88 D8 00 00 00";
    constexpr int WORLD_SYSTEM_P2_RIP_OFFSET = 12;
    constexpr int WORLD_SYSTEM_P2_RIP_END = 16;

    constexpr const char* WORLD_SYSTEM_P3 =
        "48 8B 0D ? ? ? ? 48 8B 49 50 E8 ? ? ? ? 84 C0 0F 94 C0";
    constexpr int WORLD_SYSTEM_P3_RIP_OFFSET = 3;
    constexpr int WORLD_SYSTEM_P3_RIP_END = 7;

    // --- ChildActor Vtable Resolution (from EquipHide) ---
    constexpr const char* CHILD_ACTOR_VTBL_P1 =
        "48 8B 55 08 48 89 F1 E8 ? ? ? ? 90 48 8D 05 ? ? ? ? 48 89 06 EB ?";
    constexpr int CHILD_ACTOR_VTBL_P1_RIP_OFFSET = 16;
    constexpr int CHILD_ACTOR_VTBL_P1_RIP_END = 20;

    // --- PartInOut transition (equipment visibility, from EquipHide) ---
    constexpr const char* PART_INOUT_P1 =
        "41 0F B6 45 1C 3C 03 74 ? 45 84 C0 75 ? 84 C0";

    constexpr const char* PART_INOUT_P2 =
        "45 32 C0 48 8B 4D ? 48 8B 41 38 8B 49 40 48 C1 E1 04 48 03 C8 48 3B C1 74 ? 41 8B 12";
    constexpr int PART_INOUT_P2_OFFSET = 0x36;

    // --- MapLookup (from EquipHide) ---
    constexpr const char* MAP_LOOKUP_P1 =
        "48 83 EC 08 83 79 04 00 4C 8B C1 75 ? 33 C0 48 83 C4 08 C3 48 8B 05 ? ? ? ? 48 89 1C 24 8B 1A";

    // --- MapInsert (from EquipHide) ---
    constexpr const char* MAP_INSERT_P1 =
        "4C 89 4C 24 20 53 55 56 57 41 54 41 55 48 83 EC 28 44 8B 11 48 8B D9 4D 8B E1 41 8B F0 4C 8B EA";
}

// =========================================================================
// Runtime offset storage - populated by signature scanning at init time.
// These are set by HookManager after scanning, so the rest of the code
// can use them without re-scanning.
// =========================================================================
struct RuntimeOffsets {
    uintptr_t world_system_ptr = 0;       // WorldSystem singleton address
    uintptr_t player_actor_ptr = 0;       // Current player actor base
    uintptr_t player_position_ptr = 0;    // Float* to player [X,Y,Z]
    uintptr_t player_stats_component = 0; // Stats component base
    uintptr_t actor_manager_ptr = 0;      // ActorManager for entity iteration
    uintptr_t child_actor_vtbl = 0;       // ChildActor vtable for type checking

    bool world_system_resolved = false;
    bool player_resolved = false;
    bool position_resolved = false;
};

// Global runtime offsets instance
RuntimeOffsets& get_runtime_offsets();

// Helper to read game memory safely (with null check)
template<typename T>
T read_mem(uintptr_t base, uint32_t offset) {
    if (base == 0) return T{};
    auto* ptr = reinterpret_cast<T*>(base + offset);
    return *ptr;
}

template<typename T>
bool write_mem(uintptr_t base, uint32_t offset, const T& value) {
    if (base == 0) return false;
    *reinterpret_cast<T*>(base + offset) = value;
    return true;
}

// Safe pointer chain dereference (follows pointer chain, returns 0 on null)
uintptr_t resolve_ptr_chain(uintptr_t base, std::initializer_list<uint32_t> offsets);

// Minimum valid pointer address (below this is likely invalid)
constexpr uintptr_t kMinimumPointerAddress = 0x10000000;

inline bool is_valid_ptr(uintptr_t addr) {
    return addr >= kMinimumPointerAddress;
}

} // namespace cdcoop
