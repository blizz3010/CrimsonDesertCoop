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

### What's Needed

| Priority | Task | Status |
|----------|------|--------|
| **High** | Verify animation offsets (0x120, 0x124) with ReClass | Estimated |
| **High** | Verify combat flag offsets (0x130, 0x131) | Estimated |
| **High** | Extract real animation IDs from PAZ archives | Placeholder |
| **Medium** | Find quest manager in WorldSystem for quest sync | Not started |
| **Medium** | Find cutscene manager for cutscene triggering | Not started |
| **Medium** | Map camera state offsets for co-op camera | Not started |
| **Low** | Verify rotation offset (pos_struct + 0xA0) | Estimated |
| **Low** | Find world object manager for interaction sync | Not started |

### What's Already Done

- Player entity finding via WorldSystem chain (verified offsets)
- Position read/write via authoritative pointer chain (verified)
- Health/Stamina/Spirit via StatEntry structure (verified)
- Companion hijacking via actor body slots (implemented)
- Damage tracking and co-op damage reporting (implemented)
- Enemy iteration and HP scaling (implemented)
- DX12 Present hook for ImGui overlay (implemented)
- Steam P2P networking via ISteamNetworkingSockets (implemented)
- 15+ AOB signatures with fallback patterns (from community mods)

### Related Projects & Offset Sources

- [CrimsonDesert-player-status-modifier](https://github.com/Orcax-1399/CrimsonDesert-player-status-modifier) - Player stats, position, damage signatures
- [CrimsonDesertTools](https://github.com/tkhquang/CrimsonDesertTools) - WorldSystem, actor structure, equipment visibility
- [DetourModKit](https://github.com/tkhquang/DetourModKit) - AOB scanning framework used by CD mods
- [crimson-desert-unpacker](https://github.com/lazorr410/crimson-desert-unpacker) - PAZ archive extraction tool
- [CrimsonDesertModdingResearch](https://github.com/marvelmaster/CrimsonDesertModdingResearch) - Address value table, XML configs
- [Nexus Mods Cheat Table](https://www.nexusmods.com/crimsondesert/mods/64) - Cheat Engine table with pointer paths
- [FearLess CE Community](https://fearlessrevolution.com/viewtopic.php?t=38679) - Active offset research
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
