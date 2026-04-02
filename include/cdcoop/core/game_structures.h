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
//   - Send @ Sintrix.net / FearlessRevolution - camera FOV/zoom, contribution, trust
//   - bbfox0703 (Nexus Mods #64 CT) - player base pointer, character pointer chains, inventory
//   - Tuuuup! (FearLess Revolution) - ServerActor, ATK/DEF, max stats, base supply, item struct
//
// Game version: v1.01.03 / Table v1.0.6 (March 2026)

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
// Static base pointers (from CE tables, may break on patch):
//   CrimsonDesert.exe+05CC7618  (bbfox0703, v1.01.03 - most recent)
//   CrimsonDesert.exe+05CE0928  (Send, v1.00.03)
//   CrimsonDesert.exe+05EDB400  (FearLess community)
//   CrimsonDesert.exe+05C008A0  (FearLess community)
//
// Verified full pointer chains (from bbfox0703 CT, game v1.01.03):
//   Player base = CrimsonDesert.exe+5CC7618
//   Health chain:  [base+0x18]+0xA0]+0xD0]+slot]+0x20]+0x18]+0x58]+0x08
//   Stamina chain: [base+0x18]+0xA0]+0xD0]+slot]+0x20]+0x18]+0x58]+0x488
//   Spirit chain:  [base+0x18]+0xA0]+0xD0]+slot]+0x20]+0x18]+0x58]+0x518
//   Character slot offsets: Kliff=0x68, Oongka=0xE0, Damiane=0x168
// =========================================================================
namespace offsets {

    // =========================================================================
    // Static player base pointer (from bbfox0703 CT, v1.01.03)
    // Use signature scanning (PLAYER_BASE_DISCOVERY) for dynamic resolution.
    // =========================================================================
    namespace PlayerBase {
        constexpr uint32_t STATIC_RVA     = 0x05CC7618; // CrimsonDesert.exe+5CC7618 (v1.01.03)

        // Common chain prefix from base to character body:
        // [base+0x18] -> +0xA0 -> +0xD0 -> {character_slot}
        constexpr uint32_t CHAIN_0        = 0x18;
        constexpr uint32_t CHAIN_1        = 0xA0;
        constexpr uint32_t CHAIN_2        = 0xD0;

        // Character-specific slot offsets (divergence point in chain)
        // From bbfox0703 CT chain (via body slot navigation):
        constexpr uint32_t SLOT_KLIFF     = 0x68;   // Main character
        constexpr uint32_t SLOT_OONGKA    = 0xE0;   // Companion
        constexpr uint32_t SLOT_DAMIANE   = 0x168;  // Companion

        // Alternative direct character offsets from childactor (Tuuuup! CT, v1.01.02):
        // childactor+0x68 = first, +0x168 = second, +0x268 = third
        constexpr uint32_t DIRECT_CHAR_0  = 0x68;   // First character (Kliff)
        constexpr uint32_t DIRECT_CHAR_1  = 0x168;  // Second character (Damiane)
        constexpr uint32_t DIRECT_CHAR_2  = 0x268;  // Third character (party slot 3)

        // ServerActor pointer (from Tuuuup! CT):
        // childactor+0xA0 = server-side actor representation
        constexpr uint32_t SERVER_ACTOR   = 0xA0;

        // Suffix chain from character slot to stats component:
        // +{slot} -> +0x20 -> +0x18 -> +0x58 -> {stat_offset}
        constexpr uint32_t STAT_CHAIN_0   = 0x20;
        constexpr uint32_t STAT_CHAIN_1   = 0x18;
        constexpr uint32_t STAT_CHAIN_2   = 0x58;   // Stats component base

        // ATK/DEF chain (from Tuuuup! CT, v1.01.02):
        // +{slot} -> +0x20 -> +0x18 -> +0x38 -> {offset}
        constexpr uint32_t COMBAT_CHAIN_2 = 0x38;   // Combat stats component (instead of 0x58)
        constexpr uint32_t ATK_OFFSET     = 0x00;   // int32 - attack power
        constexpr uint32_t DEF_OFFSET     = 0x08;   // int32 - defence

        // Stat offsets within stats component (+0x58)
        // Verified by both bbfox0703 (v1.01.03) and Tuuuup! (v1.01.02) CTs
        constexpr uint32_t HEALTH_OFFSET  = 0x08;   // 4-byte current HP
        constexpr uint32_t HEALTH_MAX     = 0x18;   // 4-byte max HP (from Tuuuup!)
        constexpr uint32_t STAMINA_OFFSET = 0x488;  // 4-byte current stamina
        constexpr uint32_t STAMINA_MAX    = 0x498;  // 4-byte max stamina (from Tuuuup!)
        constexpr uint32_t SPIRIT_OFFSET  = 0x518;  // 4-byte current spirit
        constexpr uint32_t SPIRIT_MAX     = 0x528;  // 4-byte max spirit (from Tuuuup!)

        // Stat ID offsets (type identifiers within stats component, from Tuuuup!):
        constexpr uint32_t HEALTH_ID_OFF  = 0x00;   // HP stat type ID
        constexpr uint32_t STAMINA_ID_OFF = 0x480;  // Stamina stat type ID
        constexpr uint32_t SPIRIT_ID_OFF  = 0x510;  // Spirit stat type ID

        // Inventory chain (from character slot):
        // +{slot} -> +0xB8 -> +0x18 -> +0x08 -> {inv_offset}
        constexpr uint32_t INV_CHAIN_0    = 0xB8;
        constexpr uint32_t INV_CHAIN_1    = 0x18;
        constexpr uint32_t INV_CHAIN_2    = 0x08;
        constexpr uint32_t INV_USED_SLOTS = 0x12;   // uint16
        constexpr uint32_t INV_TOTAL_SLOTS= 0x14;   // uint16
        constexpr uint32_t INV_BONUS_SLOTS= 0x16;   // uint16
    }

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

    namespace Camera {
        // Camera struct offsets (from Send's CE table, game v1.00.03)
        // The camera zoom/FOV write instruction:
        //   movss [r12+0xD8], xmm0
        // r12 = camera struct base during the zoom/FOV write path
        constexpr uint32_t ZOOM_FOV       = 0xD8;   // float - zoom/FOV value (default ~8.0 at max zoom out)
    }

    namespace Contribution {
        // Contribution system pointer chain (from Send's CE table, game v1.00.03)
        // Base: CrimsonDesert.exe+05CE0928
        // Chain: +0x80 -> +0x60 -> +0x208 -> +0x478 -> +0x0 -> +0x0 -> +0x10
        constexpr uint32_t STATIC_BASE_RVA = 0x05CE0928; // RVA from CrimsonDesert.exe base
        constexpr uint32_t LEVEL          = 0x08;   // int32 - contribution level (from r9+0x08)
        constexpr uint32_t EXPERIENCE     = 0x10;   // int32 - contribution experience (from r9+0x10)
    }

    namespace Trust {
        // Trust value offset within trust data struct (from Send's CE table)
        // Written via movups instructions at rdx+0x10 (gift) or rax+0x10 (shop NPC)
        constexpr uint32_t VALUE          = 0x10;   // trust value offset
    }

    // =========================================================================
    // Base/Settlement supply structure (from Tuuuup! CT, v1.01.02)
    // Found via AOB: 48 83 7B 10 00 7E 53 48 8D 4B 08 66
    // Triggered when [rbx] == 0x165 (supply data identifier)
    // =========================================================================
    namespace BaseSupply {
        constexpr uint32_t POINTS         = 0x10;   // int32
        constexpr uint32_t MONEY          = 0x250;  // int32 - base money
        constexpr uint32_t FOOD           = 0x310;  // int32 - food supplies
        constexpr uint32_t WOOD           = 0x3D0;  // int32 - wood/timber
        constexpr uint32_t ORE            = 0x490;  // int32 - ore supplies
        constexpr uint32_t CRAFT          = 0x550;  // int32 - clothing/craft
    }

    // =========================================================================
    // Item structure (from Tuuuup! CT, v1.01.02)
    // Captured from selected/hovered item hook
    // =========================================================================
    namespace ItemEntry {
        constexpr uint32_t ITEM_ID        = 0x08;   // uint16 - item identifier
        constexpr uint32_t REFINEMENT     = 0x0A;   // uint16 - refinement level
        constexpr uint32_t AMOUNT         = 0x10;   // int64 - item count
        constexpr uint32_t REINFORCEMENT  = 0x50;   // int32 - reinforcement level
        constexpr uint32_t FLAG           = 0x104;  // byte - 1=true/server, 0=visual/client
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

    // --- Player Base via ChildActor (from Tuuuup! CT, game v1.01.02) ---
    // Hook: rdi = childactor, [rdi+0xA0] = serveractor, [rdi+0x68] = first char data
    // Original bytes: 48 8B 47 68 48 8B 88 38 01 00 00
    constexpr const char* PLAYER_BASE_CHILDACTOR =
        "48 8B 47 68 48 8B 88 38 01 00 00 80";

    // --- Current Player / ServerChildOnlyInGameActor (from Tuuuup! CT, v1.01.02) ---
    // Hook: rbx = pa::ServerChildOnlyInGameActor, [rbx+0x68]+0x1B0 = controller
    constexpr const char* CURRENT_PLAYER =
        "48 ? ? ? 48 ? ? ? ? ? ? ? 48 ? ? ? 0F B7 ? ? 66 ? ? ? ? ? ? ? B8 ? ? ? ? 66 ? ? 74 ? 48 ? ? ? ? ? ? ? E8 ? ? ? ? 0F B7 ? 48 ? ? ? ? ? ? ? 48 ? ? B2 ? FF ? ? 0F B7 ? 48 ? ? ? ? ? ? ? E8 ? ? ? ? 3A";

    // --- Friendship Write (from Tuuuup!/bulle, game v1.01.02) ---
    // Hook: [rax+0x10] = friendship value
    constexpr const char* FRIENDSHIP_WRITE =
        "48 8B 58 10 48 8B 4C 24 30 4C";

    // --- Base Supply Access (from Tuuuup! CT, game v1.01.02) ---
    // Hook: rbx = supply entry, [rbx] == 0x165 identifies supply data
    constexpr const char* BASE_SUPPLY =
        "48 83 7B 10 00 7E 53 48 8D 4B 08 66";

    // --- Selected/Hovered Item (from Tuuuup! CT, game v1.01.02) ---
    // Hook: rsi = item struct, [rsi+0x104]==1 for true/server item
    constexpr const char* SELECTED_ITEM =
        "48 8B 46 10 49 89 46 10 0F";

    // --- Archery Contest (from Tuuuup! CT, game v1.01.02) ---
    // Hook: rdi = contest data, [rdi+0x10] = score, [rdi+0x14] = target
    constexpr const char* ARCHERY_CONTEST =
        "8B 4F 10 3B C1 0F 93 C2";

    // --- Player Base Discovery (from bbfox0703 CT, game v1.01.03) ---
    // RIP-relative pointer resolution to find the Player static base dynamically
    // This is more robust than hardcoding the static address (0x5CC7618)
    constexpr const char* PLAYER_BASE_DISCOVERY =
        "48 8B 0D ? ? ? ? E8 ? ? ? ? 41 B0 01 48 8B 53 08 48 8D 4C 24 40";
    constexpr int PLAYER_BASE_DISCOVERY_RIP_OFFSET = 3;
    constexpr int PLAYER_BASE_DISCOVERY_RIP_END = 7;

    // --- Item Highlight / Private Storage (from bbfox0703 CT, game v1.01.03) ---
    // Injection at CrimsonDesert.exe+1AA8DC7: cmp qword ptr [rbx+10],00
    // rsi=7 for storage view, rsi=1 for inventory view, r10=item struct
    constexpr const char* ITEM_HIGHLIGHT =
        "49 83 7A 10 00 7E 37";

    // --- Contest Score (from bbfox0703 CT, game v1.01.03) ---
    // Injection at CrimsonDesert.exe+C3F21E: mov rax,[rbx+68]
    // rbx = contest data struct, score chain: +0x68 -> +0x20 -> +0x388 -> +0x64
    constexpr const char* CONTEST_SCORE =
        "48 8B 43 68 48 8B 48 20 48 8B 81";

    // --- Contribution Gain (from bbfox0703 CT, game v1.01.03) ---
    // Injection at CrimsonDesert.exe+1B38EC1: mov r12,[r8+10]
    // r8 = contribution entry, +0x0C = current value, +0x08 = secondary, +0x10 = data ptr
    constexpr const char* CONTRIBUTION_GAIN =
        "4D 8B 60 10 89 45 C0";

    // --- Inventory Slot Read (from bbfox0703 CT, game v1.01.03) ---
    // Reads total slots (+0x14) and used slots (+0x12) as signed 16-bit
    constexpr const char* INVENTORY_SLOT_READ =
        "0F BF 48 14 0F BF 40 12 2B C8 41";

    // --- Max Slot Add (from bbfox0703 CT, game v1.01.03) ---
    // Adds bonus slots at [rbx+0x16]: add [rbx+16],di
    constexpr const char* MAX_SLOT_ADD =
        "66 01 7B 16 48 8B C6";

    // --- Camera Zoom/FOV Write (from Send's CE table, game v1.00.03) ---
    // Instruction: movss [r12+0xD8], xmm0 (writes zoom/FOV to camera struct)
    // Non-wildcard bytes: F3 41 0F 11 84 24 D8 00 00 00
    // r12 = camera struct base, offset 0xD8 = zoom/FOV value
    constexpr const char* CAMERA_ZOOM_FOV =
        "F3 41 0F 11 84 24 D8 00 00 00 F3 ? ? ? ? F3 ? ? ? ? ? ? ? ? ? ? F3 ? ? ? ? F3 ? ? ? ? ? ? ? ? ? ? F2 ? ? ? ? F2 ? ? ? ? ? ? ? ? ? ? ? 8B";
    constexpr const char* CAMERA_ZOOM_FOV_NONWILD =
        "F3 41 0F 11 84 24 D8 00 00 00";

    // --- Contribution Overworld Map (from Send's CE table, game v1.00.03) ---
    // Non-wildcard bytes: 45 8B 69 08 44 89 AD F8 02 00 00
    // At hook: r9 = contribution data struct, level at +0x08, exp at +0x10
    constexpr const char* CONTRIBUTION_MAP =
        "45 ? ? ? 44 ? ? ? ? ? ? ? E9 ? ? ? ? 49 ? ? ? 0F ? ? ? ? ? ? 45 ? ? ?";

    // --- Trust Gift Write (from Send's CE table, game v1.00.03) ---
    // Non-wildcard bytes: 0F 11 4A 10 0F 10 47 20 0F 11 42 20 0F 10 4F 30 0F 11 4A 30 F2
    // At hook: rdx = trust data struct, trust value at +0x10
    constexpr const char* TRUST_GIFT =
        "0F 11 ? 10 0F 10 ? 20 0F 11 ? 20 0F 10 ? 30 0F 11 ? 30 F2 ? ? ? ? ? ? ? F2 ? ? ? ? ? ? ? 48 83 ? ? 41 5E";

    // --- Trust Gift ShopNPC (from Send's CE table, game v1.00.03) ---
    // Non-wildcard bytes: 0F 11 50 10 0F 11 58 20 0F 11 60
    constexpr const char* TRUST_GIFT_SHOP =
        "0F 11 ? 10 0F 11 ? 20 0F 11 ? ? F2 ? ? ? ? ? ? ? 48 83";

    // --- Item Count Decrease (from Send's CE table / FearLess community) ---
    // Non-wildcard bytes: 49 29 4C 07 10
    // NOP this to prevent item count decrease
    constexpr const char* ITEM_COUNT_DECREASE =
        "49 29 4C 07 10";
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
    uintptr_t camera_struct_ptr = 0;      // Camera struct base (r12 from zoom/FOV hook)
    uintptr_t contribution_data_ptr = 0;  // Contribution data struct (r9 from contribution hook)

    bool world_system_resolved = false;
    bool player_resolved = false;
    bool position_resolved = false;
    bool camera_resolved = false;
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
