# Crimson Desert Co-op Mod

A co-op multiplayer mod for [Crimson Desert](https://store.steampowered.com/app/3321460/Crimson_Desert/) that allows two players to play through the game together. The host player's game world is shared with a second player who joins via Steam P2P networking.

> **Status: In Development - Core Systems Functional**
>
> Player position sync, companion hijacking, damage tracking, enemy HP sync, DX12 overlay, and Steam P2P networking all work with verified offsets. Animation sync is in passthrough mode (same-model only). Quest sync, cutscene sync, and world interaction sync are **not yet implemented** - they need reverse engineering work. See [Current Limitations](#current-limitations) and [Contributing](#contributing).

## How It Works

1. **Host starts a session** (press F7 in-game) - their game becomes the shared world
2. **Client connects** via Steam friend invite or Steam ID
3. **Player 2 appears** as a hijacked companion entity (Oongka/Yann/Naira) controlled by the remote player's inputs
4. **Game state syncs** through the host (enemies, damage, positions)

### Architecture

```
+-----------------+         Steam P2P          +-----------------+
|  HOST (Player 1)|<-------------------------->| CLIENT (Player 2)|
|                 |   Position, Animation,     |                  |
|  Authoritative  |   Combat, Enemy State      |  Receives world  |
|  game world     |                            |  state from host |
|                 |                            |                  |
|  Companion slot |                            |  Sends inputs &  |
|  = Player 2     |                            |  position to host|
+-----------------+                            +-----------------+
```

### Key Design Decisions

- **Companion Hijacking**: We take over an existing companion slot so the game handles rendering, collision, and animations for Player 2 natively
- **Host-Authoritative**: The host's game state is the source of truth for enemies, world interactions, and damage validation
- **Steam P2P**: Uses Steamworks `ISteamNetworkingSockets` - NAT traversal, encryption, reliable/unreliable channels, no server needed
- **ASI Injection**: Loaded via standard ASI loader. Uses [safetyhook](https://github.com/cursey/safetyhook) for mid-function hooking

## Current Limitations

These are features that are **not yet working** and require reverse engineering or community contribution:

| Feature | Status | What's Blocking It |
|---------|--------|--------------------|
| Animation sync (cross-model) | Passthrough only | Real animation IDs need to be extracted from PAZ archives. Works fine when both players use the same character model |
| Quest sync | Not implemented | Quest manager pointer not found within WorldSystem |
| Cutscene sync | Not implemented | Cutscene manager/trigger function not found |
| World interaction sync | Event logging only | World object manager layout unknown |
| Dragon mount HP | Unresolved | Confirmed float type (not int64*1000), but pointer chain unknown |
| Per-action combat flags | Estimated only | isAttacking (0x130) and isDodging (0x131) not verified |
| Full camera struct | Zoom/FOV only | Camera mods use PAZ XML, not runtime memory. Only +0xD8 is mapped |

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

### Recommended: Using a Mod Manager

This mod works with [JSON Mod Manager](https://www.nexusmods.com/crimsondesert/mods/113), which handles ASI loader setup and mod conflict detection automatically.

1. Install **JSON Mod Manager** following its instructions
2. Place `CrimsonDesertCoop.asi` and `cdcoop_config.json` into JSON Mod Manager's `mods/` folder
3. Launch the game through JSON Mod Manager

### Manual Installation

1. Download [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) (`dinput8.dll` version)
2. Copy `dinput8.dll` to your Crimson Desert game directory (next to `CrimsonDesert.exe`)
3. Copy `CrimsonDesertCoop.asi` to the same directory
4. (Optional) Copy `cdcoop_config.json` to customize settings
5. Launch the game normally through Steam

### Compatibility

- Compatible with [CDUMM](https://www.nexusmods.com/crimsondesert/mods/207) and [JSON Mod Manager](https://www.nexusmods.com/crimsondesert/mods/113)
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

Edit `cdcoop_config.json` in the game directory. The file is auto-created with defaults if missing.

```json
{
    "player_name": "Player",
    "port": 27015,
    "use_steam_networking": true,

    "enemy_hp_multiplier": 1.5,
    "enemy_dmg_multiplier": 1.0,
    "tether_distance": 150.0,

    "sync_cutscenes": false,
    "sync_quest_progress": false,
    "skip_animation_remap": true,

    "player2_model_id": -1,
    "player2_use_companion_slot": true,

    "debug_overlay": false,
    "log_packets": false,
    "log_level": 2,

    "toggle_overlay_key": 119,
    "open_session_key": 118
}
```

| Option | Default | Description |
|--------|---------|-------------|
| `player_name` | `"Player"` | Display name in co-op |
| `enemy_hp_multiplier` | `1.5` | Scale enemy HP for 2 players |
| `enemy_dmg_multiplier` | `1.0` | Scale enemy damage |
| `tether_distance` | `150.0` | Max distance between players (meters) |
| `sync_cutscenes` | `false` | Cutscene sync (not yet implemented) |
| `sync_quest_progress` | `false` | Quest sync (not yet implemented) |
| `skip_animation_remap` | `true` | Use passthrough mode for animation sync |
| `player2_model_id` | `-1` | -1 = same as host character model |
| `player2_use_companion_slot` | `true` | Hijack companion vs spawn new entity |
| `debug_overlay` | `false` | Show debug info in overlay |
| `log_packets` | `false` | Log network packets to cdcoop.log |
| `log_level` | `2` | 0=trace, 1=debug, 2=info, 3=warn, 4=error |
| `toggle_overlay_key` | `119` | F8 keycode |
| `open_session_key` | `118` | F7 keycode |

## Project Structure

```
CrimsonDesertCoop/
+-- include/cdcoop/
|   +-- core/           # Hooks, memory scanning, game structures, config
|   +-- network/        # Session management, packets, Steam P2P transport
|   +-- sync/           # Player, enemy, world, animation synchronization
|   +-- player/         # Companion hijacking, player entity management
|   +-- ui/             # ImGui overlay
+-- src/
|   +-- asi/            # DLL entry point (loaded by ASI loader)
|   +-- core/           # Hook installation, memory scanning, config I/O
|   +-- network/        # Networking implementation
|   +-- sync/           # Sync system implementations
|   +-- player/         # Player & companion management
|   +-- ui/             # Overlay rendering (DX12)
+-- tools/
|   +-- offset_scanner.py   # Helper for finding game memory offsets
+-- docs/
|   +-- REVERSE_ENGINEERING.md  # Guide for discovering game offsets
+-- CMakeLists.txt
```

## Contributing

Core systems are functional but several areas need community help. If you have experience with Cheat Engine, x64dbg, IDA/Ghidra, or ReClass.NET, see [`docs/REVERSE_ENGINEERING.md`](docs/REVERSE_ENGINEERING.md) for verified offsets and what still needs work.

### What's Needed (Missing Offsets)

These are the remaining offsets/systems needed. **If you have access to any of these, please contribute!**

| # | Priority | System | What We Need | Status |
|---|----------|--------|--------------|--------|
| 1 | **HIGH** | Animation State | Verify offsets 0x120 (anim ID) and 0x124 (blend weight) on actor base with ReClass.NET | Estimated - needs verification |
| 2 | **HIGH** | Animation IDs | Extract real animation ID table from PAZ archives for cross-model remapping | Passthrough mode for MVP |
| 3 | **MEDIUM** | Quest Manager | Find quest manager pointer within WorldSystem or a nearby singleton | Not started |
| 4 | **MEDIUM** | Cutscene Manager | Find the function/manager that triggers cutscenes | Not started |
| 5 | **MEDIUM** | Camera State | Map camera struct beyond zoom (+0xD8) - position, rotation, target | Partial (zoom only) |
| 6 | **MEDIUM** | Dragon HP | Dragon mount health offset (confirmed float type, not int64*1000) | Unresolved |
| 7 | **LOW** | World Objects | Find manager for doors, chests, interactive world objects | Not started |
| 8 | **LOW** | Teleport System | Hook fast travel to sync both players to same destination | Not started |
| 9 | **LOW** | Combat Flags | Verify isAttacking (0x130) and isDodging (0x131) on actor base | Estimated |

#### Where to Look

- **Animation**: Attach ReClass to actor base, trigger animations, watch for changing uint32/float at +0x120. Use [crimson-desert-unpacker](https://github.com/lazorr410/crimson-desert-unpacker) and [CrimsonForge](https://www.nexusmods.com/crimsondesert/mods/446) for PAZ animation extraction
- **Quest/Cutscene**: WorldSystem (+0x30 = ActorManager) likely has sibling pointers to other managers. [NattKh Save Editor](https://github.com/NattKh/CRIMSON-DESERT-SAVE-EDITOR) has 633 quests / 5,450 missions. [JustSkip](https://github.com/wealdly/JustSkip) hooks the cutscene system
- **Dragon HP**: FearLess pages 14-16 discuss this. Community tried 4-byte, float, ALL value types without success. May use a separate entity pool from ground mounts
- **Camera**: [UltimateCameraMod](https://github.com/FitzDegenhub/UltimateCameraMod) has 150+ camera states in `playercamerapreset.xml`. Try ReClass on the camera struct pointer (captured via r12 in zoom hook)

### Resources (Bot-Protected / Auth-Gated)

These pages require manual human access (403 for automated tools). **If you can access them, the offset data inside would be valuable:**

| Resource | What It Likely Contains | Priority |
|----------|------------------------|----------|
| [FearLess CE Thread (pages 14-16)](https://fearlessrevolution.com/viewtopic.php?t=38679) | Dragon HP discussion, mount pointers, new scripts | **HIGH** |
| [Nexus Mods Cheat Table v1.0.6](https://www.nexusmods.com/crimsondesert/mods/64) | Pointer chains (partially extracted from [GitHub mirror](https://github.com/bbfox0703/Mydev-Cheat-Engine-Tables)) | MEDIUM |
| [Nexus Mods Modding Guide v4.0](https://www.nexusmods.com/crimsondesert/mods/366) | BlackSpace engine internals (updated April 2026) | MEDIUM |
| [CDCamera source/patches](https://www.nexusmods.com/crimsondesert/mods/65) | Camera field mappings (distance, height, FOV, steadycam) | MEDIUM |
| [OpenCheatTables Thread](https://opencheattables.com/viewtopic.php?p=4863) | Alternative pointer paths, dragon HP progress | LOW |

### What's Already Working (Verified Offsets)

| Category | Status | Key Details |
|----------|--------|-------------|
| **Player Entity** | Verified | WorldSystem -> ActorManager -> UserActor chain, 3 fallback sigs |
| **Position** | Verified | actor->+0x40->+0x08->core->+0x248->struct->+0x90 (float32 X/Y/Z) |
| **Health/Stamina/Spirit** | Verified | StatEntry 16-byte struct via stats component at +0x58 (int64, value*1000) |
| **Rotation** | Verified | Quaternion at position_struct+0xA0, synced at 30Hz |
| **Companion System** | Verified | Body slots +0xD0 through +0x108, AI controller at +0x48 |
| **Damage Tracking** | Verified | Damage slot and value capture via dedicated hooks |
| **Enemy HP/State** | Verified | Same actor+stat layout as player; aggro at +0x150, state at +0x158 |
| **Inventory** | Verified | Chain from character slot: +0xB8->+0x18->+0x08 |
| **Item Structure** | Verified | ID at +0x08, refinement at +0x0A, amount at +0x10, reinforcement at +0x50 |
| **ATK/DEF** | Verified | Via chain: slot->+0x20->+0x18->+0x38 |
| **Camera Zoom/FOV** | Verified | Camera struct+0xD8 via r12 hook capture |
| **Base Supply** | Verified | Points, money, food, wood, ore, craft at known offsets |
| **Contribution** | Verified | Level at +0x08, experience at +0x10 |
| **Trust System** | Verified | Trust value at struct+0x10, gift and shop NPC paths |
| **Reputation** | Verified | Gain setter at +1B4C98E, no-decrease at +1B4C971 (from bbfox0703 CT) |
| **Resistance Attrs** | Verified | Injection at +12D1DFC, stride 0x20, scale 50M/level (from bbfox0703 CT) |
| **Combat State Flag** | Verified | RIP-relative AOB, combat byte at resolved_ptr+0x1A (from JustSkip) |
| **Durability** | Verified | Write, delta, and abyss AOBs with primary/fallback (from Orcax) |
| **Dragon Timer** | Verified | r13+0x160 float, AOB integrated |
| **Mount HP (Horse)** | Verified | Dynamic capture via hook steps (pointer only resolves while mounted) |
| **DX12 Present** | Implemented | Hook for ImGui overlay and frame tick |
| **Steam P2P** | Implemented | ISteamNetworkingSockets with reliable/unreliable channels |
| **40+ AOB Signatures** | Verified | Primary/fallback patterns from community mods |

### Related Projects & Offset Sources

- [CrimsonDesert-player-status-modifier](https://github.com/Orcax-1399/CrimsonDesert-player-status-modifier) - Stats, position, damage, durability signatures (ASI mod, safetyhook)
- [JustSkip](https://github.com/wealdly/JustSkip) - Combat state flag AOB and RIP-relative resolver
- [CrimsonDesertTools](https://github.com/tkhquang/CrimsonDesertTools) - WorldSystem, actor structure, equipment visibility
- [DetourModKit](https://github.com/tkhquang/DetourModKit) - AOB scanning framework used by CD mods
- [bbfox0703 Cheat Tables](https://github.com/bbfox0703/Mydev-Cheat-Engine-Tables) - 220+ entry open-source CT with reputation, friendship, durability
- [crimson-desert-unpacker](https://github.com/lazorr410/crimson-desert-unpacker) - PAZ archive extraction tool
- [pycrimson](https://github.com/LukeFZ/pycrimson) - Python PAZ/save decrypt library
- [UltimateCameraMod](https://github.com/FitzDegenhub/UltimateCameraMod) - 150+ camera states in PAZ XML
- [SWISS Knife Save Editor](https://www.nexusmods.com/crimsondesert/mods/20) - 633 quests, 5,450 missions, 2,262 items
- [CrimsonDesertModdingResearch](https://github.com/marvelmaster/CrimsonDesertModdingResearch) - Address value table, XML configs
- [CrimsonForge](https://github.com/hzeemr/crimsonforge) - Browse 1.4M game files, full PAZ round-trip
- [Elden Ring Seamless Co-op](https://github.com/LukeYui/EldenRingSeamlessCoopRelease) - Architectural inspiration

## Technical Details

### Game Info

- **Engine**: BlackSpace Engine (Pearl Abyss proprietary, evolved from BDO engine)
- **DRM**: Denuvo Anti-Tamper (no kernel-level anti-cheat)
- **Graphics API**: DirectX 12
- **Architecture**: x64 Windows
- **Game Version**: v1.01.03 (March 2026)

### Networking Protocol

All packets use a binary format with a `CD` magic header:

```
[2 bytes magic "CD"] [1 byte type] [2 bytes payload size] [4 bytes sequence] [4 bytes timestamp] [payload...]
```

Position updates are sent unreliable at 30 Hz. Combat actions and world state changes are sent reliable. A heartbeat packet maintains the connection with a 10-second timeout.

## License

This project is for educational and personal use. Use at your own risk. Not affiliated with Pearl Abyss.
