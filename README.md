# Crimson Desert Co-op Mod

A co-op multiplayer mod for [Crimson Desert](https://store.steampowered.com/app/3321460/Crimson_Desert/) that allows two players to play through the game together. The host player's game world is shared with a second player who joins via Steam P2P networking.

> **Status: In Development - Core Systems Functional**
> Memory offsets and signatures have been integrated from the active Crimson Desert modding community. Player position tracking, companion hijacking, damage sync, and stat reading use verified offsets. DX12 overlay and Steam P2P networking are implemented. Some animation offsets and world interaction systems still need fine-tuning via RE. See [Contributing](#contributing).

## How It Works

1. **Host starts a session** (press F7 in-game) - their game becomes the shared world
2. **Client connects** via Steam friend invite or Steam ID
3. **Player 2 appears** as a hijacked companion entity (Oongka/Yann/Naira) controlled by the remote player's inputs
4. **All game state** (enemies, world interactions, quests) syncs through the host

### Architecture

```
┌─────────────────┐         Steam P2P          ┌─────────────────┐
│   HOST (Player 1)│◄──────────────────────────►│ CLIENT (Player 2)│
│                  │   Position, Animation,     │                  │
│  Authoritative   │   Combat, World State      │  Receives world  │
│  game world      │                            │  state from host │
│                  │                            │                  │
│  Companion slot  │                            │  Sends inputs &  │
│  = Player 2      │                            │  position to host│
└─────────────────┘                            └─────────────────┘
```

### Key Design Decisions

- **Companion Hijacking**: Instead of spawning a new entity type, we take over an existing companion slot (the game already has AI companions). This means the game handles rendering, collision, and animations for Player 2 natively.
- **Host-Authoritative**: The host's game state is the source of truth. Enemy HP, world interactions, and quest progress are all controlled by the host. This prevents desync and simplifies the networking model.
- **Steam P2P**: Uses Steamworks `ISteamNetworkingSockets` for networking - provides NAT traversal, encryption, and reliable/unreliable channels with no dedicated server needed.
- **ASI Injection**: Loaded via standard ASI loader into the game process. Uses [safetyhook](https://github.com/cursey/safetyhook) for mid-function hooking.

## Building

### Requirements

- **Windows 10/11** (the game is Windows-only)
- **Visual Studio 2022** with C++ desktop development workload
- **CMake 3.20+**
- **Steamworks SDK** (download from [Steamworks](https://partner.steamgames.com/))

### Build Steps

```bash
# Clone the repository
git clone https://github.com/blizz3010/CrimsonDesertCoop.git
cd CrimsonDesertCoop

# Configure with CMake
cmake -B build -G "Visual Studio 17 2022" -A x64 \
    -DSTEAMWORKS_SDK_PATH="C:/path/to/steamworks_sdk"

# Build
cmake --build build --config Release
```

The output `CrimsonDesertCoop.asi` will be in `build/bin/Release/`.

### Building without Steam (for testing)

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCDCOOP_USE_STEAM=OFF
cmake --build build --config Release
```

## Installation

### Recommended: Using CDUMM (Crimson Desert Ultimate Mods Manager)

This mod works best with [CDUMM - Crimson Desert Ultimate Mods Manager](https://www.nexusmods.com/crimsondesert/mods/207), which handles ASI loader setup and mod conflict detection automatically.

1. Install **CDUMM** following its instructions
2. Place `CrimsonDesertCoop.asi` and `cdcoop_config.json` into CDUMM's `mods/` folder
3. Launch the game through CDUMM

### Manual Installation

1. Download [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) (`dinput8.dll` version)
2. Copy `dinput8.dll` to your Crimson Desert game directory (next to `CrimsonDesert.exe`)
3. Copy `CrimsonDesertCoop.asi` to the same directory
4. (Optional) Copy `cdcoop_config.json` to customize settings
5. Launch the game normally through Steam

### Compatibility

- Compatible with [CDUMM](https://www.nexusmods.com/crimsondesert/mods/207) mod manager
- Compatible with [JSON Mod Manager](https://www.nexusmods.com/crimsondesert/mods/113) (place ASI in `mods/` folder)
- Compatible with most PAZ-based mods (camera, visuals, equipment)
- **May conflict** with other ASI mods that hook the same game functions (damage, position writes). If using alongside the [player-status-modifier](https://www.nexusmods.com/crimsondesert/mods/52), disable overlapping features in the config

## Usage

| Key | Action |
|-----|--------|
| **F7** | Host a co-op session / Open join dialog |
| **F8** | Toggle the co-op overlay UI |

### Hosting

1. Press **F7** to start hosting
2. Share your Steam ID with your friend, or use the Steam overlay to invite them
3. Your friend needs the mod installed too - they press F7 and enter your Steam ID to join

### Configuration

Edit `cdcoop_config.json` in the game directory:

```json
{
    "player_name": "Player",
    "enemy_hp_multiplier": 1.5,
    "enemy_dmg_multiplier": 1.0,
    "tether_distance": 150.0,
    "sync_cutscenes": true,
    "sync_quest_progress": true,
    "debug_overlay": false
}
```

## Project Structure

```
CrimsonDesertCoop/
├── include/cdcoop/
│   ├── core/           # Hooks, memory scanning, game structures, config
│   ├── network/        # Session management, packets, Steam P2P transport
│   ├── sync/           # Player, enemy, world, animation synchronization
│   ├── player/         # Companion hijacking, player entity management
│   └── ui/             # ImGui overlay
├── src/
│   ├── asi/            # DLL entry point (loaded by ASI loader)
│   ├── core/           # Hook installation, memory scanning, config I/O
│   ├── network/        # Networking implementation
│   ├── sync/           # Sync system implementations
│   ├── player/         # Player & companion management
│   └── ui/             # Overlay rendering (DX12)
├── tools/
│   └── offset_scanner.py   # Helper for finding game memory offsets
├── docs/
│   └── REVERSE_ENGINEERING.md  # Guide for discovering game offsets
└── CMakeLists.txt
```

## Contributing

Core systems are functional but several areas need community help. If you have experience with Cheat Engine, x64dbg, IDA/Ghidra, or ReClass.NET, see [`docs/REVERSE_ENGINEERING.md`](docs/REVERSE_ENGINEERING.md) for verified offsets and what still needs work.

### What's Needed (Offsets Still Missing)

These are the remaining offsets/systems needed to bring the mod to completion. **If you have access to any of these, please contribute!**

| # | Priority | System | What We Need | Status | Where to Look |
|---|----------|--------|--------------|--------|---------------|
| 1 | **HIGH** | Animation State | Verify offsets 0x120 (anim ID) and 0x124 (blend weight) on actor base with ReClass.NET | Estimated - needs verification | Attach ReClass to actor base, trigger animations, watch for changing uint32/float |
| 2 | **HIGH** | Animation IDs | Extract real animation ID table from PAZ archives for cross-model remapping | Using passthrough mode for MVP - real IDs still needed for cross-model remapping | Use [crimson-desert-unpacker](https://github.com/lazorr410/crimson-desert-unpacker) to extract animation config XMLs. [CrimsonForge](https://www.nexusmods.com/crimsondesert/mods/446) can extract Havok .hkx with bone names, skeleton hierarchy, and animation type info. Filter with `--filter "*.xml"` for animation configs |
| 3 | **MEDIUM** | Quest Manager | Find quest manager pointer within WorldSystem or a nearby singleton | Not started | WorldSystem (+0x30 = ActorManager) likely has sibling pointers to other managers. [NattKh Save Editor](https://github.com/NattKh/CRIMSON-DESERT-SAVE-EDITOR) has quest inject via PARC insert with 63,000+ validated offsets. SWISS Knife has 633 quests / 5,450 missions |
| 4 | **MEDIUM** | Cutscene Manager | Find the function/manager that triggers cutscenes | Not started | Set breakpoint on known cutscene entry, trace call stack. JustSkip mod (ASI) skips cutscenes natively - study its hook point for the cutscene trigger function |
| 5 | **MEDIUM** | Camera State (full) | Map complete camera struct beyond zoom (+0xD8). Need position, rotation, target, interpolation fields | Partial - zoom only. Camera mods use PAZ XML (`playercamerapreset.xml`), not runtime memory | [UltimateCameraMod](https://github.com/FitzDegenhub/UltimateCameraMod) has 150+ camera states in `playercamerapreset.xml`. [CDCamera mod](https://www.nexusmods.com/crimsondesert/mods/65) PAZ patches have field layout. Only +0xD8 is memory-mapped |
| 6 | **MEDIUM** | Mount HP (Dragon) | Dragon mount health, stamina, and flight timer offsets | **Unresolved** - confirmed float type (not int64*1000) | FearLess pages 14-16: dragon HP hard to find. May be float not int4. Community tried 4-byte, float, ALL value types without success. Separate entity pool from ground mounts? Dragon timer AOB integrated but HP pointer unknown |
| 7 | **LOW** | World Object Manager | Find manager for doors, chests, interactive world objects | Not started | MapLookup/MapInsert sigs exist from EquipHide work but exact manager layout unknown. May be accessible from WorldSystem sibling pointers |
| 8 | **LOW** | Teleport/Fast Travel System | Hook fast travel to sync both players to same destination | Not started | [Save Editor](https://github.com/NattKh/CRIMSON-DESERT-SAVE-EDITOR) can inject 5,500+ knowledge entries including fast travel unlocks - may hint at data structure |
| 9 | **LOW** | Combat Flags (per-action) | Verify 0x130 (isAttacking) and 0x131 (isDodging) bool offsets on actor base | Estimated - combat state byte verified, per-action flags still need verification | ReClass.NET: watch bytes at actor+0x130 region while attacking/dodging. Global combat state flag already integrated from JustSkip |

### Resources You Can Help With (Bot-Protected / Auth-Gated)

These pages are blocked from automated access (403/auth-gated). **If you can access them manually, the offset data inside would be extremely valuable:**

| Resource | URL | What It Likely Contains | Priority |
|----------|-----|------------------------|----------|
| FearLess CE Thread (main, 16+ pages) | [fearlessrevolution.com/viewtopic.php?t=38679](https://fearlessrevolution.com/viewtopic.php?t=38679) | **Pages 14-16 have dragon HP discussion**, mount pointers, new scripts. Horse HP confirmed, dragon HP still elusive | **HIGH** |
| FearLess CE Thread (secondary, bbfox) | [fearlessrevolution.com/viewtopic.php?t=38655](https://fearlessrevolution.com/viewtopic.php?t=38655) | BBFox's original offset research - real player address for inventory and stats | MEDIUM |
| Nexus Mods Cheat Table v1.0.6 (.CT file) | [nexusmods.com/crimsondesert/mods/64](https://www.nexusmods.com/crimsondesert/mods/64) | Download .CT and open in CE - we've partially extracted from [GitHub mirror](https://github.com/bbfox0703/Mydev-Cheat-Engine-Tables) but Nexus version may have newer pointer chains | MEDIUM |
| Nexus Mods Modding Guide v4.0 | [nexusmods.com/crimsondesert/mods/366](https://www.nexusmods.com/crimsondesert/mods/366) | Updated April 2026 for JSON Mod Manager V9. May document BlackSpace engine internals | MEDIUM |
| CDCamera source/patches | [nexusmods.com/crimsondesert/mods/65](https://www.nexusmods.com/crimsondesert/mods/65) | Camera overhaul PAZ patches - field mappings for camera struct (distance, height, FOV, steadycam) | MEDIUM |
| OpenCheatTables Thread | [opencheattables.com/viewtopic.php?p=4863](https://opencheattables.com/viewtopic.php?p=4863) | Alternative CE community - may have different pointer paths or dragon HP progress | MEDIUM |
| AutoLoot Cheat Table | [nexusmods.com/crimsondesert/mods/93](https://www.nexusmods.com/crimsondesert/mods/93) | AutoLoot CE table - item pickup / loot system offsets | LOW |
| Crimson Desert Forge | [nexusmods.com/crimsondesert/mods/446](https://www.nexusmods.com/crimsondesert/mods/446) | PAZ round-trip tool. Can extract Havok .hkx (skeleton/animation) but no RE docs found | LOW |
| FLiNG Trainer (latest) | [flingtrainer.com/trainer/crimson-desert-trainer/](https://flingtrainer.com/trainer/crimson-desert-trainer/) | Updated March 30, 2026 - 10 options. May use different offset paths than CE tables | LOW |
| ResHax PAZ/PAMT Discussion | [reshax.com/topic/18908](https://reshax.com/topic/18908-need-help-extracting-paz-pamt-files-from-crimson-desert-blackspace-engine/) | PAZ/PAMT extraction details - animation file format info needed for animation ID extraction | LOW |
| WeMod Trainer | [wemod.com/cheats/crimson-desert-trainers](https://www.wemod.com/cheats/crimson-desert-trainers) | 10 cheats including defense editing - may use unique offset paths | LOW |

### What's Already Done (Verified Offsets)

- **Player Entity** - WorldSystem -> ActorManager -> UserActor chain (verified, 3 fallback sigs)
- **Position** - Authoritative pointer chain: actor->+0x40->+0x08->core->+0x248->struct->+0x90 (X/Y/Z floats, verified)
- **Health/Stamina/Spirit** - StatEntry 16-byte structure via stats component at +0x58 (verified)
- **Companion System** - Actor body slots +0xD0 through +0x108, AI controller at +0x48 (verified)
- **Damage Tracking** - Damage slot and value capture via dedicated hooks (verified)
- **Enemy HP/State** - Same actor base + stat component as player; aggro at +0x150, AI state at +0x158 (verified)
- **Inventory** - Chain from character slot: +0xB8->+0x18->+0x08 with used/total/bonus slots (verified)
- **Item Structure** - ID at +0x08, refinement at +0x0A, amount at +0x10, reinforcement at +0x50 (verified)
- **ATK/DEF** - Via chain: slot->+0x20->+0x18->+0x38 (ATK at +0x00, DEF at +0x08, verified)
- **Camera Zoom/FOV** - Camera struct+0xD8, hook captures r12 as camera base (verified)
- **Base Supply** - Points, money, food, wood, ore, clothing at known offsets (verified)
- **Contribution System** - Level at +0x08, experience at +0x10 from contribution data struct (verified)
- **Trust System** - Trust value at struct+0x10, gift and shop NPC write paths (verified)
- **Item Count Decrease** - Instruction `49 29 4C 07 10` for preventing item loss (verified)
- **Equipment Visibility** - Body->VisCtrl chain: +0x68->+0x40->+0xE8, PartInOutSocket at +0x1C (verified)
- **Reputation System** - Gain setter at +1B4C98E, no-decrease at +1B4C971, current at [rax+0x08], min at [rax+0x04] (from bbfox0703 CT, newly integrated)
- **Friendship System** - Fast friendship injection at +14F639E, value at [rax+0x10] (from bbfox0703 CT, newly integrated)
- **Combat State Flag** - RIP-relative AOB `48 8B 05 ?? ?? ?? ?? 80 78 1A 01`, combat byte at resolved_ptr+0x1A (from [JustSkip](https://github.com/wealdly/JustSkip) source, newly integrated)
- **Durability System** - Write, delta, and abyss durability AOBs with primary/fallback patterns (from [Orcax player-status-modifier](https://github.com/Orcax-1399/CrimsonDesert-player-status-modifier) source, newly integrated)
- **DX12 Present** - Hook for ImGui overlay and frame tick (implemented)
- **Steam P2P** - ISteamNetworkingSockets with reliable/unreliable channels (implemented)
- **Player Rotation** - Quaternion at position_struct+0xA0, read via verified pointer chain in position hook at 30Hz (fixed, previously sent identity rotation)
- **Resistance Attributes** - Full struct from bbfox0703 CT: injection at +12D1DFC, stride 0x20, ATK +0x000, DEF +0x020, Cold +0x340, Fire +0x360, Lightning +0x3A0, scale 50M/level (verified, fully integrated)
- **Dragon Timer Offset** - r13+0x160 (float, from community CT - AOB integrated, hook point needs verification)
- **Mount HP (Horse)** - Dynamic HP capture via hook steps at +12D78BA / +12D09BE, uses same StatEntry format as player (from bbfox0703 CT - pointer only resolves while mounted)
- **40+ AOB Signatures** - With primary/fallback patterns from community mods (expanded April 2026)

### Related Projects & Offset Sources

- [CrimsonDesert-player-status-modifier](https://github.com/Orcax-1399/CrimsonDesert-player-status-modifier) - Player stats, position, damage, durability signatures (ASI mod, safetyhook, updated March 2026)
- [JustSkip](https://github.com/wealdly/JustSkip) - Game speed control ASI with **combat state flag AOB** and generic `ScanSignature()` + RIP-relative resolver (open source, MinHook)
- [CrimsonDesertTools](https://github.com/tkhquang/CrimsonDesertTools) - WorldSystem, actor structure, equipment visibility
- [DetourModKit](https://github.com/tkhquang/DetourModKit) - AOB scanning framework used by CD mods
- [crimson-desert-unpacker](https://github.com/lazorr410/crimson-desert-unpacker) - PAZ archive extraction tool
- [pycrimson](https://github.com/LukeFZ/pycrimson) - Python PAZ/save decrypt library with reflection deserializer
- [bbfox0703 Cheat Tables](https://github.com/bbfox0703/Mydev-Cheat-Engine-Tables) - **220+ entry open-source CT** with reputation, friendship, durability offsets
- [UltimateCameraMod](https://github.com/FitzDegenhub/UltimateCameraMod) - 150+ camera states in `playercamerapreset.xml`
- [SWISS Knife Save Editor](https://www.nexusmods.com/crimsondesert/mods/20) - 633 quests, 5,450 missions, 2,262 items, 5,500+ knowledge entries
- [CrimsonDesertModdingResearch](https://github.com/marvelmaster/CrimsonDesertModdingResearch) - Address value table, XML configs
- [CrimsonForge](https://github.com/hzeemr/crimsonforge) - Browse 1.4M game files, mesh/audio replacement, full PAZ round-trip
- [Nexus Mods Cheat Table](https://www.nexusmods.com/crimsondesert/mods/64) - Cheat Engine table with pointer paths
- [FearLess CE Community](https://fearlessrevolution.com/viewtopic.php?t=38679) - Active offset research (16+ pages)
- [BDO RE Article](https://secret.club/2019/01/24/reverse-engineering-bdo-2.html) - BlackSpace engine lineage RE (RTTI, actor proxy classes)
- [CDUMM Mod Manager](https://www.nexusmods.com/crimsondesert/mods/207) - Recommended mod manager
- [Elden Ring Seamless Co-op](https://github.com/LukeYui/EldenRingSeamlessCoopRelease) - Architectural inspiration

## Technical Details

### Game Info

- **Engine**: BlackSpace Engine (Pearl Abyss proprietary, evolved from BDO engine)
- **DRM**: Denuvo Anti-Tamper (no kernel-level anti-cheat)
- **Graphics API**: DirectX 12
- **Architecture**: x64 Windows

### Networking Protocol

All packets use a simple binary format with a `CD` magic header:

```
[2 bytes magic "CD"] [1 byte type] [2 bytes payload size] [4 bytes sequence] [4 bytes timestamp] [payload...]
```

Position updates are sent unreliable at 30 Hz. Combat actions and world state changes are sent reliable. A heartbeat packet maintains the connection with a 10-second timeout.

## License

This project is for educational and personal use. Use at your own risk. Not affiliated with Pearl Abyss.
