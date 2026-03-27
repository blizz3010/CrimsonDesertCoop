# Crimson Desert Co-op Mod

A co-op multiplayer mod for [Crimson Desert](https://store.steampowered.com/app/3321460/Crimson_Desert/) that allows two players to play through the game together. The host player's game world is shared with a second player who joins via Steam P2P networking.

> **Status: Early Development / Framework Stage**
> The mod architecture and networking layer are implemented. Game-specific memory offsets need to be discovered via reverse engineering before the mod is functional. See [Contributing](#contributing).

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

1. Download [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) (`dinput8.dll` version)
2. Copy `dinput8.dll` to your Crimson Desert game directory (next to `CrimsonDesert.exe`)
3. Copy `CrimsonDesertCoop.asi` to the same directory
4. (Optional) Copy `cdcoop_config.json` to customize settings
5. Launch the game normally through Steam

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

The biggest need right now is **reverse engineering** the game's memory structures. If you have experience with Cheat Engine, x64dbg, IDA/Ghidra, or ReClass.NET, see [`docs/REVERSE_ENGINEERING.md`](docs/REVERSE_ENGINEERING.md) for a detailed guide on what needs to be found.

### What's Needed

| Priority | Task | Difficulty |
|----------|------|-----------|
| **Critical** | Find player entity memory offsets (position, health, anim state) | Medium |
| **Critical** | Find companion system/manager pointer and struct layout | Medium |
| **Critical** | Find game tick/update function signature | Medium |
| **High** | Find damage calculation function signature | Medium |
| **High** | Implement DX12 Present hook for ImGui overlay | Hard |
| **Medium** | Discover enemy manager and entity list structure | Medium |
| **Medium** | Map animation IDs for player combat moves | Easy-Medium |
| **Low** | Find camera system for co-op camera adjustments | Medium |

### Related Projects

- [CrimsonDesert-player-status-modifier](https://github.com/Orcax-1399/CrimsonDesert-player-status-modifier) - Proves ASI hooking works, has some player offsets
- [crimson-desert-unpacker](https://github.com/lazorr410/crimson-desert-unpacker) - PAZ archive extraction tool
- [CrimsonDesertModdingResearch](https://github.com/marvelmaster/CrimsonDesertModdingResearch) - Community RE research
- [Elden Ring Seamless Co-op](https://github.com/LukeYui/EldenRingSeamlessCoopRelease) - Inspiration for the architecture

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
