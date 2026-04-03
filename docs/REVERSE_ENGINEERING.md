# Reverse Engineering Guide for Crimson Desert Co-op

This document explains how to find the memory offsets and function signatures needed to make the co-op mod work. **All offsets in the code are placeholders** and must be filled in via reverse engineering.

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
4. Take damage, search for new value → repeat until 1-2 results remain
5. The address you find is `Player + HEALTH_OFFSET`
6. Use "Find what accesses this address" to discover the **player base pointer**
7. In the dissect data structures view, map out the player struct

### Method B: Signature from existing mod

The `CrimsonDesert-player-status-modifier` project already hooks player stat writes. Study their signatures and hook points to find the player pointer - it's typically the first argument (`rcx` on x64 Windows) to the hooked function.

### Expected Player Structure Layout

```
Player Entity (approximate, verify all offsets):
+0x???  Vec3     Position (x, y, z)
+0x???  Quat     Rotation (x, y, z, w)
+0x???  float    Health
+0x???  float    MaxHealth
+0x???  float    Stamina
+0x???  uint32   AnimationState
+0x???  float    AnimBlendWeight
+0x???  bool     IsAttacking
+0x???  bool     IsDodging
+0x???  uint32   WeaponId
+0x???  float    MovementSpeed
```

## Step 2: Find the Companion System

Companions (Oongka, Yann, Naira) are key to the co-op approach - we hijack one for Player 2.

1. With a companion active in-game, search for the companion's health
2. Find the companion entity pointer using same technique as player
3. Look for an **AI Controller** pointer on the companion (likely a pointer to a class that handles AI decisions)
4. The companion manager is typically reachable from the game instance:
   `GameInstance -> SomeManager -> CompanionArray[]`

### Key things to find:
- Companion entity base address
- AI Controller offset (we null this to disable AI)
- Position offset (we write player 2's position here)
- Animation state offset (we write player 2's animations here)
- Is Active flag (to check if companion is spawned)

## Step 3: Find the Game Instance / World Singleton

Most game engines have a singleton "game instance" or "world" object:

1. In x64dbg, set a breakpoint on the player position write
2. When hit, examine `rcx` (first arg) - trace back to find how this pointer is obtained
3. Look for patterns like:
   ```asm
   mov rax, [some_static_address]  ; Game instance singleton
   mov rcx, [rax + offset]         ; Player controller
   mov rcx, [rcx + offset]         ; Player entity
   ```
4. The `some_static_address` is your game instance pointer

## Step 4: Find Function Signatures

For each hook, we need an **IDA-style signature** (byte pattern with wildcards):

### Game Tick / Update
- Set a conditional breakpoint on player position read
- Find the calling function that runs every frame
- Extract unique bytes from the function prologue

### Companion Spawn
- Dismiss and resummon a companion
- Set a hardware breakpoint on the companion entity pointer
- Capture the function that allocates the new companion entity

### Damage Calculation
- Set a breakpoint on the health write
- Step up the call stack to find the damage calculation function
- This typically takes (attacker, target, baseDamage) and returns final damage

### Camera Update
- The camera position updates every frame
- Search for the camera's position in memory
- Hook the function that writes to it

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
- Fill in all `constexpr uint32_t` offset values

Edit `src/core/hooks.cpp`:
- Uncomment and fill in the `create_hook()` calls with real signatures

## Useful Tools

### Offset Scanner (included)
Run `tools/offset_scanner.py` with Cheat Engine's Python API to automate offset discovery.

### PAZ Unpacker
The [crimson-desert-unpacker](https://github.com/lazorr410/crimson-desert-unpacker) can extract game configuration XMLs from PAZ archives. These may contain entity structure definitions, animation IDs, and other useful data.

## Community Resources

- [Nexus Mods - Crimson Desert](https://www.nexusmods.com/crimsondesert) - Existing mods and tools
- [FearLess Cheat Engine Forum](https://fearlessrevolution.com/) - CE tables with known offsets
- [ReHax Forum](https://reshax.com/) - PAZ/PAMT extraction discussion
- [CrimsonDesertModdingResearch](https://github.com/marvelmaster/CrimsonDesertModdingResearch) - Community RE efforts

## Known Offsets and Signatures (March 2026)

The following offsets and signatures have been sourced from the active modding community:

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

### WorldSystem Chain (from EquipHide)
- WorldSystem is a singleton found via RIP-relative pointer in signature scan
- ActorManager at WorldSystem + 0x30
- UserActor (player) at ActorManager + 0x28

### Position Data (Verified)
The position system has two access methods:

**Method 1: Authoritative position chain** (from position_research.md):
```
actor -> +0x40 -> +0x08 -> player_core -> +0x248 -> position_struct -> +0x90
```
- `position_struct + 0x90` = X axis (float, verified)
- `position_struct + 0x94` = Y axis (float, verified)
- `position_struct + 0x98` = Z axis (float, verified)
- `position_struct + 0x9C` = W/padding (float)
- Position write instruction at `CrimsonDesert.exe+36ADB8C`: `41 0F 11 45 00` (movups [r13+00], xmm0)

**Method 2: Hook-time direct access** (via PositionHeightAccess sig):
- r13 = float* pointing directly at the position vector
- xmm0 contains position components
- Position is float[3]: X at +0x00, Y/height at +0x04, Z at +0x08

**Position cache (NOT authoritative, may lag):**
```
player_core -> +0xA0 -> cache_block
```

**Status marker chain:** `[rdx+0x68] -> [rax+0x20]`

**Candidate static base pointers** (unstable, prefer sig scanning):
- `CrimsonDesert.exe+05EDB400`
- `CrimsonDesert.exe+05C008A0`

### Static Player Base Pointer (from bbfox0703 CT, v1.01.03)
```
Player = CrimsonDesert.exe+5CC7618
```
Discovery AOB (RIP-relative): `48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 41 B0 01 48 8B 53 08 48 8D 4C 24 40`

### Character Pointer Chains (from bbfox0703 CT, v1.01.03)
All characters share a common chain prefix:
```
[Player+0x18] → +0xA0 → +0xD0 → {character_slot} → +0x20 → +0x18 → +0x58 → {stat}
```

Character slot offsets (point of divergence):
| Character | Slot Offset | Role |
|-----------|-------------|------|
| Kliff     | **0x68**    | Main player character |
| Oongka    | **0xE0**    | Companion |
| Damiane   | **0x168**   | Companion |

Stat offsets within the stats component (+0x58):
| Stat | Offset | Type |
|------|--------|------|
| Health | **+0x08** | 4-byte int (displayed * 1000) |
| Stamina | **+0x488** | 4-byte int |
| Spirit | **+0x518** | 4-byte int |

### Inventory Structure (from bbfox0703 CT, v1.01.03)
Chain from character slot: `→ +0xB8 → +0x18 → +0x08 → {offset}`
| Field | Offset | Type |
|-------|--------|------|
| Used Slots | +0x12 | uint16 |
| Total Slots | +0x14 | uint16 |
| Bonus Slots | +0x16 | uint16 |

Item highlight read AOB: `0F BF 48 14 0F BF 40 12 2B C8 41`
Max slot write AOB: `66 01 7B 16 48 8B C6`

### ServerActor vs ChildActor (from Tuuuup! CT, v1.01.02)
The game has a dual-actor architecture:
- `childactor` = the in-game entity (what you see)
- `serveractor` = `[childactor+0xA0]` - server-side authoritative state

Direct character offsets from childactor:
| Slot | Offset | Character |
|------|--------|-----------|
| 0 | +0x68  | First (Kliff) |
| 1 | +0x168 | Second (Damiane) |
| 2 | +0x268 | Third (party slot 3) |

### ATK / Defence (from Tuuuup! CT, v1.01.02)
Via chain: `+{slot} → +0x20 → +0x18 → +0x38 → {offset}`
| Stat | Offset |
|------|--------|
| ATK | +0x00 |
| Defence | +0x08 |

### Max Stat Values (from Tuuuup! CT, v1.01.02)
| Stat | Current | Max |
|------|---------|-----|
| Health | +0x08 | +0x18 |
| Stamina | +0x488 | +0x498 |
| Spirit | +0x518 | +0x528 |

### Base Supply Structure (from Tuuuup! CT, v1.01.02)
AOB: `48 83 7B 10 00 7E 53 48 8D 4B 08 66`
| Resource | Offset |
|----------|--------|
| Points | +0x10 |
| Money | +0x250 |
| Food | +0x310 |
| Wood/Timber | +0x3D0 |
| Ore | +0x490 |
| Clothing/Craft | +0x550 |

### Item Structure (from Tuuuup! CT, v1.01.02)
| Field | Offset | Type |
|-------|--------|------|
| Item ID | +0x08 | uint16 |
| Refinement | +0x0A | uint16 |
| Amount | +0x10 | int64 |
| Reinforcement | +0x50 | int32 |
| Server/Visual flag | +0x104 | byte (1=server) |

### Key Signatures (IDA-style, ? = wildcard)
See `include/cdcoop/core/game_structures.h` namespace `signatures` for the full list.

### Camera Zoom/FOV (from Send's CE table, v1.00.03)
The camera zoom/FOV value is written by:
```asm
movss [r12+0xD8], xmm0    ; F3 41 0F 11 84 24 D8 00 00 00
```
- `r12` = camera struct base pointer
- Offset `0xD8` = zoom/FOV float value
- Default: ~8.0 at max zoom out
- AOB: `F3 41 0F 11 84 24 D8 00 00 00`

### Contribution System (from Send's CE table, v1.00.03)
- Static base: `CrimsonDesert.exe+05CE0928`
- Pointer chain to contribution points: +0x80 → +0x60 → +0x208 → +0x478 → +0x0 → +0x0 → +0x10
- Contribution data struct (captured from hook via r9):
  - Level at +0x08 (int32)
  - Experience at +0x10 (int32)
- AOB: `45 8B 69 08 44 89 AD F8 02 00 00`

### Trust System (from Send's CE table, v1.00.03)
- Trust value at struct+0x10
- Gift write AOB: `0F 11 4A 10 0F 10 47 20 0F 11 42 20 0F 10 4F 30 0F 11 4A 30 F2`
- ShopNPC write AOB: `0F 11 50 10 0F 11 58 20 0F 11 60`

### Item Count (from FearLess community)
- Decrease instruction: `49 29 4C 07 10` (sub [r15+rax+10], rcx)
- NOP to prevent item count decrease

### Community Resources
- [CrimsonDesert-player-status-modifier](https://github.com/Orcax-1399/CrimsonDesert-player-status-modifier) - Player stats, position, damage hooks
- [CrimsonDesertTools](https://github.com/tkhquang/CrimsonDesertTools) - WorldSystem, actor structure, equipment visibility
- [DetourModKit](https://github.com/tkhquang/DetourModKit) - AOB scanning framework used by CD mods
- [CrimsonDesertModdingResearch](https://github.com/marvelmaster/CrimsonDesertModdingResearch) - Address value table, XML configs
- [Nexus Mods Cheat Table](https://www.nexusmods.com/crimsondesert/mods/64) - Cheat Engine table with pointer paths
- [FearLess CE Thread](https://fearlessrevolution.com/viewtopic.php?t=38679) - Community cheat tables, pointer research
- [FearLess CE Trust/FOV/Contribution](https://fearlessrevolution.com/viewtopic.php?t=38691) - Camera FOV, contribution, trust offsets by Send

## New Leads from Community Research (April 2026)

The following leads were identified during a comprehensive research session and may contain new offsets we haven't integrated yet.

### bbfox0703 Cheat Table (HIGHEST VALUE - Open Source CT with 220+ entries)
**GitHub**: https://github.com/bbfox0703/Mydev-Cheat-Engine-Tables/blob/main/Crimson%20Desert/CrimsonDesert.CT
- This is the **most detailed open-source CT** available - full XML source with 220+ entries
- **New offsets NOT yet in our codebase**:

| Feature | Injection Address | Details |
|---------|------------------|---------|
| Fast Friendship | `CrimsonDesert.exe+14F639E` | Caps `[rax+10]` at `0x64` (100) |
| Infinite Items | `CrimsonDesert.exe+1D3992A` | Item structure: ID at `+0x08`, qty at `+0x10`, flags at `+0x20` |
| Item Decrease Filter | `CrimsonDesert.exe+1ABCA25` | Range clamp on subtraction |
| Menu Item Amount | `CrimsonDesert.exe+F8DA43A` | Stack size thresholds |
| Bag Bonus Multiplier | `CrimsonDesert.exe+1D39628` | SSE/SIMD multiplication at `+0x14`/`+0x16` |
| Min Reputation Lock | `CrimsonDesert.exe+1B4C98E` | Rep struct: min at `+0x04`, current at `+0x08`, max at `+0x10` |
| No Reputation Decrease | `CrimsonDesert.exe+1B4C971` | Increments rep if below `0xC8` (200) |
| Weapon Durability | Entry ID 218 | No decrease |
| Gear Durability | Entry IDs 206-207 | No decrease |

- **Item ID mapping**: Arrows=#1, Silver=#1580, Abyss Cube=#2270, Materials=#800-950, Recipes=#1003-1305
- **Action needed**: Clone this repo, parse the .CT XML, extract all pointer chains and injection addresses

### Reputation System (NEW - not in our codebase)
From bbfox0703 CT - we don't have reputation offsets yet:
- Reputation min at struct `+0x04` (int32)
- Reputation current at struct `+0x08` (int32)
- Reputation max at struct `+0x10` (int32)
- Write at `CrimsonDesert.exe+1B4C98E` / `CrimsonDesert.exe+1B4C971`
- **Action needed**: Integrate reputation sync for co-op

### Cheat Evolution Trainer (28 cheats - has teleport/coordinates!)
**URL**: https://cheatevolution.com/crimson-desert-trainer/
- Has **Save Coordinates** and **Teleport to Saved Coordinates** features
- Also: movement speed multiplier, jump height multiplier, FOV edit, super movement speed, super jump
- **Action needed**: Teleportation is solved by this trainer. Movement speed multiplier offset useful for tethering
- **Accessibility**: Requires purchase/subscription

### UltimateCameraMod (150+ camera states in XML!)
**GitHub**: https://github.com/FitzDegenhub/UltimateCameraMod
- Contains **150+ camera states** defined in `playercamerapreset.xml`
- Full PAZ decrypt/repack pipeline
- **Action needed**: Extract the camera preset XML - maps every camera field to specific parameters. Could solve our camera struct mapping entirely.

### pycrimson Python Library (NEW - PAZ extraction tool)
**GitHub**: https://github.com/LukeFZ/pycrimson
- First public proper key derivation implementation for both asset and save files
- PAZ/PAMT extraction, DDS decompression, save decrypt/re-encrypt, reflection-based deserializer
- **Action needed**: Use this to programmatically extract animation data from PAZ archives (solving our animation ID problem)

### PAZ Encryption Full Spec (from community RE)
- PAMT index: filename, offset, comp_size, orig_size, flags
- Compression flags (bits 16-19): 0=none, 2=LZ4, 3=proprietary, 4=zlib
- ChaCha20 with Jenkins hashlittle key derivation (init `0xC5EDE`)
- 32-byte key: 8 chunks of `(seed ^ 0x60616263) ^ delta[i]`
- Delta table: `[0x00, 0x0A0A0A0A, 0x0C0C0C0C, 0x06060606, 0x0E0E0E0E, 0x0A0A0A0A, 0x06060606, 0x02020202]`

### Nexus Mods Forum - Enemy HP Pointer Chain (NEW)
**URL**: https://forums.nexusmods.com/topic/13533151-crimson-desert-progress-on-building-a-combat-overlay-enemy-hp-pointer-chain-needed/
- Someone is actively researching entity pointer chains for a combat overlay
- **Action needed**: Check this thread for enemy entity iteration methods

### BDO Reverse Engineering (Engine Lineage)
**URL**: https://secret.club/2019/01/24/reverse-engineering-bdo-2.html
- BDO (same engine family) documented `ge::SelfPlayerActorProxy` and `ge::PlayerActorProxy` class hierarchies at `+0xBF8`
- RTTI present in BDO builds (enables ReClass structure auto-identification)
- **Action needed**: Check if Crimson Desert binary has RTTI symbols

### Community / Discord Channels
- **Crimson Mods Discord**: https://discord.com/invite/crimson-mods-849726791530315817 (modding-focused)
- **Official Crimson Desert Discord**: https://discord.gg/crimsondesert (12,000+ members)
- **Nexus Mods**: 256+ mods in first 2 weeks post-launch

### Save Editor Data (NattKh/CRIMSON-DESERT-SAVE-EDITOR)
The save editor has reverse-engineered significant game data:
- **2,262 item templates** with full stat breakdowns (damage, defense, speed, crit, resistances)
- **633 quests and 5,450 missions** with localized names - useful for quest sync offset cross-referencing
- **189 abyss gems** across 30+ categories
- **5,500+ knowledge entries** including fast travel unlocks, recipes, map reveals
- **Save format**: PARC binary serialization, ChaCha20 encrypted, LZ4 HC compressed
- **Mount/vehicle respawn timers** are editable - suggests a vehicle manager struct exists in memory

### Mount/Horse System (FearLess CE Community)
- Horse HP and Stamina pointers exist but **only resolve while mounted**
- Changing horse requires game restart for pointer re-resolution
- Health Regen cheat now covers Health/Stamina/Mount Regen (all mount types including dragons)
- Dragon HP may be stored as float (not 4-byte int) - community struggled to find it with standard int scan
- **Action needed**: Scan for float values while on dragon, or trace the mount entity from the player actor

### Camera System (CDCamera Mod)
- The CDCamera mod modifies distance, height, FOV, steadycam, centered framing, and combat zoom
- These are PAZ-based patches (modifying game config files, not memory hooks)
- The camera struct base is captured via r12 in our zoom hook
- **Action needed**: Map fields beyond +0xD8 (zoom). Try ReClass on the r12 pointer to find position, rotation, target, and interpolation fields

### Inventory Expander (maxlehot1234)
- Uses dynamic signature scanning against 0.paz at runtime
- Survives game patches because it doesn't hardcode file offsets
- **Action needed**: Review their sig patterns for inventory-related structures we might be missing

### Gameplay Rebalance Mod (Nexus Mods #435)
- INI-configurable gameplay rebalance - may expose additional stat/combat offsets through its config

### AutoLoot Table (Nexus Mods #93)
- Cheat Engine table for auto-looting
- **Action needed**: Download and inspect for loot/pickup system offsets

## Game Version Tracking

Offsets WILL change with game patches. Maintain a version table:

| Game Version | Position Sig | Stats Sig | WorldSystem Sig | Notes |
|-------------|-------------|-----------|-----------------|-------|
| 1.00.02     | Verified    | Verified  | Verified        | Launch version |
| 1.00.03     | Verified    | Verified  | Verified        | March 25 patch - minor ptr adjustments |
| 1.01.03     | Verified    | Verified  | Verified        | March hotfix |

Use the signature scanner to automatically find updated offsets after patches rather than hardcoding addresses.
