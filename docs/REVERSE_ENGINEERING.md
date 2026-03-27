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

## Game Version Tracking

Offsets WILL change with game patches. Maintain a version table:

| Game Version | Player Position | Player Health | Companion AI | Notes |
|-------------|----------------|---------------|-------------|-------|
| 1.0.0       | TODO           | TODO          | TODO        | Launch version |

Use the signature scanner to automatically find updated offsets after patches rather than hardcoding addresses.
