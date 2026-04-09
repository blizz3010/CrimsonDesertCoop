# Reverse Engineering Guide for Crimson Desert Co-op

This document explains how to find the memory offsets and function signatures needed for the co-op mod. Many offsets are already verified and integrated - see the **Current Status** section below for what's done and what still needs work.

## Current Status (April 2026, Game v1.01.03)

| Category | Status | Details |
|----------|--------|---------|
| Player entity / WorldSystem chain | **Verified** | 3 fallback sigs, static base pointer, dynamic resolution |
| Position (authoritative) | **Verified** | actor->+0x40->+0x08->core->+0x248->struct->+0x90 (float32) |
| Health / Stamina / Spirit | **Verified** | StatEntry at +0x58, int64 (value*1000) |
| Rotation | **Verified** | Quaternion at position_struct+0xA0 |
| Companion / body slots | **Verified** | +0xD0 through +0x108 (8 slots), AI controller at +0x48 |
| Damage tracking | **Verified** | Damage slot hook with source/amount capture |
| Enemy HP / state | **Verified** | Same actor layout; aggro +0x150, state +0x158 |
| Inventory / items | **Verified** | Full item struct, inventory chain from character slot |
| Reputation | **Verified** | Gain setter at +1B4C98E, no-decrease at +1B4C971 |
| Resistance attributes | **Verified** | Injection at +12D1DFC, stride 0x20 |
| Camera zoom/FOV | **Verified** | +0xD8 via r12 capture |
| Dragon timer | **Verified** | r13+0x160 float |
| Mount HP (horse) | **Verified** | Dynamic capture (pointer only valid while mounted) |
| 40+ AOB signatures | **Verified** | Primary/fallback patterns |
| Animation state | **Likely wrong** | 0x120 / 0x124 estimated, but CDAnimCancel shows animation runs via .paac action charts, not simple actor fields |
| Combat flags (per-action) | **Estimated** | 0x130 / 0x131 - CDAnimCancel found evaluator flag at `[rbx+0x6A]` but on evaluator struct, not actor base |
| Quest manager | **Missing** | Not found |
| Cutscene manager | **Missing** | Not found |
| Full camera struct | **Missing** | Only zoom at +0xD8; rest is PAZ XML-controlled |
| Dragon HP | **Missing** | Confirmed float type, pointer chain unknown |
| World object manager | **Missing** | MapLookup/MapInsert sigs exist but manager unknown |

## Prerequisites

- **x64dbg** - Free debugger for finding function signatures
- **Cheat Engine** - Memory scanner for finding data structures
- **ReClass.NET** - Visual structure reconstruction tool
- **Ghidra** or **IDA Pro** - Disassemblers for static analysis
- A copy of Crimson Desert installed locally

## Important Notes

- Crimson Desert uses **Denuvo Anti-Tamper** (DRM), NOT kernel-level anti-cheat
- ASI injection and memory hooking are proven to work (see [CrimsonDesert-player-status-modifier](https://github.com/Orcax-1399/CrimsonDesert-player-status-modifier))
- The game uses the **BlackSpace Engine** (proprietary, no public documentation)
- Game files are in **PAZ archives** (ChaCha20 encrypted, LZ4 compressed) - see [crimson-desert-unpacker](https://github.com/lazorr410/crimson-desert-unpacker)

## Step 1: Find the Player Entity

### Method A: Cheat Engine (Recommended for beginners)

1. Open Crimson Desert and load into the game world
2. Attach Cheat Engine to the process
3. Search for your **Health** value (float, exact value)
4. Take damage, search for new value -> repeat until 1-2 results remain
5. The address you find is `Player + HEALTH_OFFSET`
6. Use "Find what accesses this address" to discover the **player base pointer**
7. In the dissect data structures view, map out the player struct

### Method B: Signature from existing mod

The `CrimsonDesert-player-status-modifier` project already hooks player stat writes. Study their signatures and hook points to find the player pointer - it's typically the first argument (`rcx` on x64 Windows) to the hooked function.

### Verified Player Structure Layout

```
Player Actor Base:
+0x00   ptr      VTable
+0x20   ptr      Status marker
+0x40   ptr      -> +0x08 -> player_core (VERIFIED)
+0x48   ptr      Component link / AI controller
+0x58   ptr      Stats component base (VERIFIED)
+0xD0   ptr[8]   Body slots (companion/child actors, +0x08 stride)

Player Core -> Position:
+0x248  ptr      -> position_struct (VERIFIED)
  +0x90 float    X position (VERIFIED)
  +0x94 float    Y position (VERIFIED)
  +0x98 float    Z position (VERIFIED)
  +0x9C float    W/padding
  +0xA0 float[4] Rotation quaternion (VERIFIED)

Stats Component (+0x58):
+0x08  int64    Health current (value * 1000) (VERIFIED)
+0x18  int64    Health max (VERIFIED)
+0x488 int64    Stamina current (VERIFIED)
+0x498 int64    Stamina max (VERIFIED)
+0x518 int64    Spirit current (VERIFIED)
+0x528 int64    Spirit max (VERIFIED)

Animation (ESTIMATED - likely incorrect approach):
+0x120 uint32   Animation state ID (estimated, but CDAnimCancel research shows
                 animation runs via .paac action charts, not simple actor fields)
+0x124 float    Blend weight (estimated)
+0x130 bool     IsAttacking (estimated, unverified by entire community)
+0x131 bool     IsDodging (estimated)
+0x134 uint32   WeaponId (estimated)
+0x140 float    MovementSpeed (estimated)
```

## Step 2: Find the Companion System

Companions (Oongka, Yann, Naira) are key to the co-op approach - we hijack one for Player 2.

The companion system is already verified:
- Companions are found via body slots on the player actor (+0xD0 through +0x108)
- AI Controller at +0x48 (null it to disable AI)
- Same position/animation layout as player
- IS_ACTIVE flag at +0x1C (PartInOutSocket visible byte)

Character slot offsets (from bbfox0703 CT, v1.01.03):
| Character | Slot Offset |
|-----------|-------------|
| Kliff     | 0x68        |
| Oongka    | 0xE0        |
| Damiane   | 0x168       |

## Step 3: Find the Game Instance / World Singleton

**Already verified.** The WorldSystem singleton is found via RIP-relative signature scanning:

```
WorldSystem (sig scan) -> ActorManager (+0x30) -> UserActor (+0x28)
```

Three fallback patterns exist in `game_structures.h` (WORLD_SYSTEM_P1/P2/P3).

Static base pointers (backup, may break on patch):
- `CrimsonDesert.exe+05CC7618` (bbfox0703, v1.01.03)
- `CrimsonDesert.exe+05CFE230` (ClientActorManager)

## Step 4: Find Function Signatures

For each hook, we need an **IDA-style signature** (byte pattern with wildcards).

See `include/cdcoop/core/game_structures.h` namespace `signatures` for all 40+ patterns with primary/fallback variants.

### Currently Installed Hooks

| Hook | Signature | Purpose |
|------|-----------|---------|
| PositionAccess | `POSITION_PRIMARY` / `POSITION_FALLBACK` | Position broadcasting (r13 = float* position) |
| DamageSlot | `DAMAGE_SLOT_PRIMARY` | Damage tracking (r15 = source, r12 = amount) |
| StatWrite | `STAT_WRITE_PRIMARY` / `STAT_WRITE_FALLBACK` | Health/stamina/spirit interception |
| CameraZoomFOV | `CAMERA_ZOOM_FOV` / `CAMERA_ZOOM_FOV_NONWILD` | Camera struct capture (r12+0xD8) |
| WorldSystem | `WORLD_SYSTEM_P1` / `P2` / `P3` | WorldSystem singleton resolution |

### Hooks Defined But Not Yet Installed (Need Signatures)

| Hook | Blocker |
|------|---------|
| `player_animation_hook` | Animation state offsets (0x120/0x124) are estimated, no animation write AOB found |
| `companion_spawn_hook` | No companion spawn function signature found |

## Step 5: Extract Signatures

Once you've found a function in x64dbg:

1. Copy the first ~20 bytes of the function
2. Replace varying bytes (addresses, offsets) with `?`
3. Test the signature in Cheat Engine's AOB scan to verify uniqueness

Example:
```
Actual bytes:  48 89 5C 24 08 57 48 83 EC 20 48 8B D9 E8 AB CD EF 01
Signature:     48 89 5C 24 ? 57 48 83 EC 20 48 8B D9 E8 ? ? ? ?
```

## Step 6: Update the Code

Edit `include/cdcoop/core/game_structures.h`:
- Fill in any remaining `constexpr uint32_t` offset values
- Add new signatures to the `signatures` namespace

Edit `src/core/hooks.cpp`:
- Add `create_hook()` calls with verified signatures

## Useful Tools

### Offset Scanner (included)
Run `tools/offset_scanner.py` with Cheat Engine's Python API to automate offset discovery.

### PAZ Unpacker
The [crimson-desert-unpacker](https://github.com/lazorr410/crimson-desert-unpacker) can extract game configuration XMLs from PAZ archives. These may contain entity structure definitions, animation IDs, and other useful data.

### PAZ Encryption Spec (from community RE)
- PAMT index: filename, offset, comp_size, orig_size, flags
- Compression flags (bits 16-19): 0=none, 2=LZ4, 3=proprietary, 4=zlib
- ChaCha20 with Jenkins hashlittle key derivation (init `0xC5EDE`)
- 32-byte key: 8 chunks of `(seed ^ 0x60616263) ^ delta[i]`

## Known Offsets and Signatures (April 2026)

The following offsets and signatures have been sourced from the active modding community. All are integrated in `include/cdcoop/core/game_structures.h`.

### Stat Entry Structure (Health / Stamina / Spirit)
From FearLess CE community and CrimsonDesert-player-status-modifier:
- Health, stamina, and spirit are **8-byte values** (int64, displayed value * 1000)
- All three share the same write opcode
- Stat type at +0x00 (0=Health, 17=Stamina, 18=Spirit)
- Current value at +0x08, Max value at +0x18
- Stats component is at actor base + 0x58, entries are 16 bytes each

### Actor Structure (from EquipHide RE work)
- VTable at +0x00
- Component link / AI controller at +0x48
- Body slots (child actors) at +0xD0 through +0x108 (8 slots, 8 bytes each)
- Body -> VisCtrl chain: +0x68 -> +0x40 -> +0xE8
- Actor type detection: +0x48 -> +0x08 -> +0x88 -> +0x01 (type byte)

### WorldSystem Chain (from EquipHide)
- WorldSystem is a singleton found via RIP-relative pointer in signature scan
- ActorManager at WorldSystem + 0x30
- UserActor (player) at ActorManager + 0x28

### Position Data (Verified)
**Authoritative position chain:**
```
actor -> +0x40 -> +0x08 -> player_core -> +0x248 -> position_struct -> +0x90
```
- Position is float32 components (4 bytes each, verified)
- Position write instruction at `CrimsonDesert.exe+36ADB8C`: `41 0F 11 45 00` (movups [r13+00], xmm0)

**Hook-time direct access** (via PositionHeightAccess sig):
- r13 = float* pointing directly at the position vector

### Static Player Base Pointer (from bbfox0703 CT, v1.01.03)
```
Player = CrimsonDesert.exe+5CC7618
```
Chain: `[base+0x18] -> +0xA0 -> +0xD0 -> {character_slot} -> +0x20 -> +0x18 -> +0x58 -> {stat}`

### Stamina/Spirit Offset Note
The Orcax player-status-modifier source defines `kStaminaEntryOffsetFromHealth = 0x480` and `kSpiritEntryOffsetFromHealth = 0x510`. Our code uses `+0x488` and `+0x518` from the stats component base. The 8-byte difference is because Orcax measures from the health entry pointer (root+0x58+0x08 = the current value field), while our offsets measure from the stats component root (root+0x58). Both are correct in their respective reference frames.

### Reputation System (from bbfox0703 CT, v1.01.03)
Fully integrated in `game_structures.h`:
- Gain setter at `CrimsonDesert.exe+1B4C98E`
- No-decrease at `CrimsonDesert.exe+1B4C971`
- Current at `[rax+0x08]`, minimum at `[rax+0x04]`, delta at `[rax+0x0C]`

### Resistance Attributes (from bbfox0703 CT v1.0.6)
Fully integrated:
- Injection at `CrimsonDesert.exe+12D1DFC`
- Stride 0x20 per entry, scale 50M per level (max level 15)
- ATK +0x000, DEF +0x020, Cold +0x340, Fire +0x360, Lightning +0x3A0

### Camera Zoom/FOV (from Send's CE table, v1.00.03)
```asm
movss [r12+0xD8], xmm0    ; F3 41 0F 11 84 24 D8 00 00 00
```
- `r12` = camera struct base pointer, offset `0xD8` = zoom/FOV float
- Full camera struct beyond +0xD8 is unmapped - camera mods use PAZ XML (`playercamerapreset.xml`)

### Key Signatures
See `include/cdcoop/core/game_structures.h` namespace `signatures` for the full list of 40+ patterns.

## New Leads from Community Research (April 2026)

### CDAnimCancel / Guard Cancel (Animation System RE)
**GitHub**: https://github.com/faisalkindi/CDAnimCancel
- **Critical finding**: The animation system uses **.paac action chart files** (PA Action Chart binary), NOT simple actor struct fields. Memory scanning for animation state offsets was "unsuccessful -- state hidden behind unknown pointers"
- **Evaluator function**: `CrimsonDesert.exe+2712090` - gate that returns 0/1 for animation transitions. AOB: `0F 28 CE 48 89 4C 24 20 48 8B CB E8`
- **Guard activation**: Entry at `+2712330`, AOB: `48 8B C4 48 89 58 10 48 89 68 18 48 89 70 20 57 41 54 41 55 41 56 41 57 48 83 EC 60`
- **Candidate array struct**: `[rbx+0x40]` = array ptr, `[rbx+0x48]` = count, `[rbx+0x68]` = current state, `[rbx+0x6A]` = active flag (0x01)
- **Each transition candidate**: 0xD0 (208) bytes
- **Includes `extract_paac.py`**: 1,368-line binary format decoder for .paac files (428 animation .paa paths in sword_upper.paac alone)
- **Implication**: Our estimated actor+0x120/0x124 animation offsets are likely the wrong approach. Real animation state lives in deserialized .paac runtime objects
- **Themida note**: CRC protection reverts executable code patches silently. Only system DLL hooks and SafetyHook work

### bbfox0703 Cheat Table (Open Source, 220+ entries)
**GitHub**: https://github.com/bbfox0703/Mydev-Cheat-Engine-Tables/blob/main/Crimson%20Desert/CrimsonDesert.CT
- Most detailed open-source CT available. Reputation, friendship, durability, resistance attributes all extracted and integrated.

### UltimateCameraMod (150+ camera states in XML)
**GitHub**: https://github.com/FitzDegenhub/UltimateCameraMod
- Full PAZ decrypt/repack pipeline with 150+ camera state definitions
- Could help map the runtime camera struct beyond +0xD8

### pycrimson Python Library (PAZ extraction)
**GitHub**: https://github.com/LukeFZ/pycrimson
- PAZ/PAMT extraction, DDS decompression, save decrypt/re-encrypt, reflection-based deserializer
- Could programmatically extract animation data from PAZ archives

### Save Editor Data (NattKh/CRIMSON-DESERT-SAVE-EDITOR)
- 2,262 item templates, 633 quests, 5,450 missions, 5,500+ knowledge entries
- Mount/vehicle respawn timers are editable - suggests a vehicle manager struct exists

### BDO Reverse Engineering (Engine Lineage)
**URL**: https://secret.club/2019/01/24/reverse-engineering-bdo-2.html
- BDO (same engine family) documented actor proxy class hierarchies
- Check if Crimson Desert binary has RTTI symbols

## Community Resources

- [CrimsonDesert-player-status-modifier](https://github.com/Orcax-1399/CrimsonDesert-player-status-modifier) - Player stats, position, damage hooks
- [CDAnimCancel](https://github.com/faisalkindi/CDAnimCancel) - Animation system RE, .paac format parser, evaluator AOBs
- [CrimsonDesertTools](https://github.com/tkhquang/CrimsonDesertTools) - WorldSystem, actor structure, equipment visibility (v0.5.1, April 8 2026)
- [DetourModKit](https://github.com/tkhquang/DetourModKit) - AOB scanning framework
- [CrimsonDesertModdingResearch](https://github.com/marvelmaster/CrimsonDesertModdingResearch) - Address value table
- [JustSkip](https://github.com/wealdly/JustSkip) - Combat state flag, cutscene skip hooks
- [Nexus Mods - Crimson Desert](https://www.nexusmods.com/crimsondesert) - 256+ mods
- [FearLess CE Thread](https://fearlessrevolution.com/viewtopic.php?t=38679) - Active offset research (16+ pages)

## Game Version Tracking

Offsets WILL change with game patches. Maintain a version table:

| Game Version | Position Sig | Stats Sig | WorldSystem Sig | Notes |
|-------------|-------------|-----------|-----------------|-------|
| 1.00.02     | Verified    | Verified  | Verified        | Launch version |
| 1.00.03     | Verified    | Verified  | Verified        | March 25 patch |
| 1.01.03     | Verified    | Verified  | Verified        | March hotfix (current) |

Use the signature scanner to automatically find updated offsets after patches rather than hardcoding addresses.
