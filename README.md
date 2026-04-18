# Crimson Desert Co-op Mod

A co-op multiplayer mod for [Crimson Desert](https://store.steampowered.com/app/3321460/Crimson_Desert/) that allows two players to play through the game together. The host player's game world is shared with a second player who joins via Steam P2P networking.

> **Status: 0.2.2 In Development - Core Systems Functional, Research Hooks Opt-In**
>
> Player position sync, companion hijacking, damage tracking, enemy HP sync, DX12 overlay, and Steam P2P networking all work with verified offsets. Animation sync defaults to passthrough but will switch to a CDAnimCancel-derived evaluator hook when `enable_experimental_hooks` is set. Dragon HP is resolved on the fly via a float-plausibility scan at first dragon mount. The fast-travel mid-hook captures the host's waypoint targets when `sync_fast_travel` is enabled — the apply path on the receiver is still log-only pending field testing. Mount HP / stamina now broadcasts both ways when `sync_mount_state` is enabled so each player sees the other's mount status in the overlay. Quest sync, cutscene sync, and world interaction sync remain **not applied server-side** - set `dump_world_system_probe` to generate telemetry that identifies the manager pointers. See [What's New in 0.2.2](#whats-new-in-022), [Current Limitations](#current-limitations), and [Contributing](#contributing).

## What's New in 0.2.2

- **Mount HP / stamina sync (opt-in)** - With `sync_mount_state = true`, `MountSync` installs a `SafetyHookMid` on the Orcax `MOUNT_PTR_CAPTURE` AOB to capture the local mount entity, polls HP/stamina from its stats component at 5Hz, and broadcasts a new `MOUNT_STATE` packet. Both peers read each other's mount state and the session panel displays a dedicated "Peer's Mount" HP + stamina bar when the remote player is mounted. Edge-triggered: mount and dismount events broadcast immediately without waiting for the next tick. Read-only on the remote side — no writes to the local mount entity, no attempt to spawn a mount for the companion. See the [Current Limitations](#current-limitations) table for the visual-sync caveat.

## What's New in 0.2.1

- **Fast-travel mid-hook (opt-in)** - With `sync_fast_travel = true`, `HookManager` now installs a `SafetyHookMid` at `CrimsonDesert.exe+0xAB5594` (the map-waypoint apply site documented in [`docs/RESEARCH_2026-04-18.md`](docs/RESEARCH_2026-04-18.md) #7). On the host, the detour reads `[r15+0x1C..0x28]` (waypoint X/Y/Z) and `[r15+0x00]` (waypoint type id), then broadcasts a new `TELEPORT_TRIGGER` packet. The client-side apply is intentionally log-only for this release because the apply function (the one that consumes the staged target and triggers the area transition) hasn't been identified — the existing 30Hz position broadcast already pulls the companion entity along once the host arrives.
- **First mid-function hook in the project** - `HookManager::create_mid_hook()` and the `safetyhook::Context`-based detour signature are now available for any future register-state captures (quest manager dispatch, cutscene trigger, etc.).
- **Two new community AOBs** in `signatures::` - `MOUNT_PTR_CAPTURE` and `MOUNT_STAMINA_ACCESS` (from Orcax-1399 player-status-modifier scanner). Documented but not yet wired into a sync path; intended as the foundation for mount HP / stamina co-op sync.
- **Build cleanup** - Fixed C2027 undefined `SteamNetConnectionStatusChangedCallback_t` regression that broke Windows builds with the Steamworks SDK ([#18](https://github.com/blizz3010/CrimsonDesertCoop/pull/18)). Fixed C4456 shadowed local in the position detour.

## What's New in 0.2.0

- **Animation evaluator hook (experimental)** - A new hook based on [CDAnimCancel](https://github.com/faisalkindi/CDAnimCancel)'s research captures the real animation evaluator struct. When captured, `AnimationSync` writes to `evaluator+0x00 / +0x04` (state / blend) instead of the deprecated `actor+0x120 / +0x124` fields that the community has shown to be inert.
- **Dragon HP auto-scan** - On first dragon mount the mod walks `marker+0x08..+0x200` looking for a plausible float HP value (100 <= v <= 1e7). The chosen offset is cached in `RuntimeOffsets::dragon_hp_offset` and surfaced in the debug overlay. Read-only, no writes.
- **WorldSystem sibling probe** - With `dump_world_system_probe = true`, the mod walks `WorldSystem+0x30..+0x100` and logs every sibling pointer's vtable RVA to `cdcoop_world_probe.log`. This is telemetry to help the community identify the quest / cutscene / world-object managers. Best-guess candidates are cached in `RuntimeOffsets` for on-demand use.
- **Hook-install telemetry** - `HookManager::status()` now exposes `installed / failed` counts and the list of hook names that succeeded vs. failed. The debug overlay surfaces this so users on a new game patch can see at a glance which signature broke.
- **Primary/fallback signature try-helper** - `try_hook_pair()` cleaned up the repetitive primary/fallback install blocks and standardised the logging prefix.
- **Config gating** - Experimental behavior is opt-in via two new flags: `enable_experimental_hooks` (default `false`) and `dump_world_system_probe` (default `false`). Default install keeps today's stable behavior.
- **MSVC /W4 + /permissive- on cdcoop_core** - Tightened compiler warnings to catch common issues in project sources without affecting third-party deps.
- **Offset cleanup** - The estimated actor animation offsets (`0x120 / 0x124 / 0x130 / 0x131`, etc.) are kept but clearly marked `DEPRECATED` in `game_structures.h`. The new canonical path is `offsets::AnimationEvaluator::*`. Stamina/spirit offset relationship (`entry + CURRENT_VALUE`) is now documented inline.

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

These features are **still not fully functional in-game** and need further research or live-process testing:

| Feature | Status | What's Blocking It |
|---------|--------|--------------------|
| Animation sync (cross-model) | Passthrough only (0.1.x behavior) + evaluator write path (experimental) | Per-model animation IDs still need PAZ extraction for true remap. The evaluator hook captures the right struct but the ID namespace differs per character model |
| Fast-travel sync | Capture-only mid-hook (opt-in via `sync_fast_travel`) | Hook reads the host's waypoint and broadcasts `TELEPORT_TRIGGER`; receive-side is log-only because the apply function isn't identified. Position broadcast usually pulls the companion along once the host arrives |
| Quest sync | Receive-side stub + candidate pointer logging | Quest manager identity is now probed - set `dump_world_system_probe=true`, reproduce progress on host, and send `cdcoop_world_probe.log` to the issue tracker |
| Cutscene sync | Receive-side stub + candidate pointer logging | Same probe as above. Trigger function still unknown |
| World interaction sync | Receive-side stub + candidate pointer logging | Same probe as above |
| Dragon mount HP | Dynamically resolved at first mount (experimental) | Auto-scan picks a plausible float. Value surfaces in the debug overlay - verify against in-game HP bar and report back if wrong |
| Mount HP / stamina sync | State broadcast + overlay (opt-in via `sync_mount_state`) | `MOUNT_PTR_CAPTURE` mid-hook captures the local mount entity, `MountSync` polls HP/stamina at 5Hz and broadcasts `MOUNT_STATE`. Both peers display each other's mount status in the session panel. Visual sync (the companion entity still hovers at mount height on the remote side because no mount entity is spawned there) is a follow-up |
| Per-action combat flags | Evaluator `+0x6A` flag captured (experimental) | Works only when evaluator hook is enabled and a combat action is active. Actor-base `0x130 / 0x131` stay deprecated |
| Full camera struct | Zoom/FOV only | Camera mods use PAZ XML, not runtime memory. Only `+0xD8` is mapped |

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
    "sync_fast_travel": false,
    "sync_mount_state": false,
    "skip_animation_remap": true,

    "player2_model_id": -1,
    "player2_use_companion_slot": true,

    "debug_overlay": false,
    "log_packets": false,
    "log_level": 2,

    "enable_experimental_hooks": false,
    "dump_world_system_probe": false,

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
| `sync_cutscenes` | `false` | Cutscene sync (receive-side still stubbed) |
| `sync_quest_progress` | `false` | Quest sync (receive-side still stubbed) |
| `sync_fast_travel` | `false` | Install the map-waypoint mid-hook. Host broadcasts captured waypoints; receive-side is log-only for now |
| `sync_mount_state` | `false` | Install mount pointer capture. Both peers broadcast mount HP/stamina so the overlay can show the other player's mount status |
| `skip_animation_remap` | `true` | Use passthrough mode for animation sync |
| `player2_model_id` | `-1` | -1 = same as host character model |
| `player2_use_companion_slot` | `true` | Hijack companion vs spawn new entity |
| `debug_overlay` | `false` | Show debug info + hook status in overlay |
| `log_packets` | `false` | Log network packets to cdcoop.log |
| `log_level` | `2` | 0=trace, 1=debug, 2=info, 3=warn, 4=error |
| `enable_experimental_hooks` | `false` | Install CDAnimCancel animation-evaluator hook and dragon HP probe. Disable if mod becomes unstable after a game patch |
| `dump_world_system_probe` | `false` | Walk WorldSystem sibling pointers once after resolve and log their vtable RVAs to `cdcoop_world_probe.log`. Safe, read-only telemetry |
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
| 1 | **HIGH** | Animation IDs per model | Extract animation paths from .paac files (428 `.paa` refs in `sword_upper.paac` alone). [CDAnimCancel](https://github.com/faisalkindi/CDAnimCancel) has an `extract_paac.py` parser. Needed for true cross-model remap | Evaluator hook exists (experimental) but the ID namespace is still same-model-only |
| 2 | **MEDIUM** | Quest Manager | Confirm which WorldSystem sibling slot from `cdcoop_world_probe.log` is the quest manager, then identify its "set stage" entry function | Probe scaffolding landed; waiting on community log submissions |
| 3 | **MEDIUM** | Cutscene Manager | Same probe approach; find the cutscene trigger function | Probe scaffolding landed; waiting on logs |
| 4 | **MEDIUM** | Camera State | Map camera struct beyond zoom (`+0xD8`) - position, rotation, target | Partial (zoom only) |
| 5 | **MEDIUM** | Dragon HP verification | Read `[dragon_mount+0xD8]` in-game and confirm it tracks the HP bar. Strong candidate from the dragon-mount field map at `CrimsonDesert.exe+0x339D8CB` (see [`docs/RESEARCH_2026-04-18.md`](docs/RESEARCH_2026-04-18.md)). Integrated as `Mount::DRAGON_HP_PREFERRED_OFFSET` | **Candidate known**, needs field read-back |
| 6 | **LOW** | World Objects | Confirm WorldSystem sibling for doors / chests / interactive world objects | Probe candidate stored but not yet mapped to a dispatch function |
| 7 | **LOW** | Teleport apply function | The capture mid-hook at `+0xAB5594` is **landed** (0.2.1, gated on `sync_fast_travel`) and broadcasts `TELEPORT_TRIGGER`. What's still missing is the apply/transition function — the one that consumes `[r14+0xD8] / [r14+0xE0]` and triggers a real area transition. Once that's identified, the receive-side stub in `WorldSync::on_remote_teleport()` can call it directly | **Capture landed**, apply path unknown |
| 8 | **LOW** | Combat Flag verification | The evaluator `+0x6A` flag from CDAnimCancel now writes when experimental hooks are enabled - confirm it actually gates co-op combat state the way we want | New hook needs field testing |
| 9 | **LOW** | Mount visual sync | **0.2.2 landed mount state sync** via the `MOUNT_PTR_CAPTURE` mid-hook + `MOUNT_STATE` packet + `MountSync`. What's still missing is visual parity: the companion entity appears to hover at mount height on the remote side because no mount entity is spawned for it. A full fix needs the mount-spawn function (unknown). An intermediate fix could piggyback on the companion's own native mount when it spawns during single-player |
| 10 | **LOW** | `MOUNT_STAMINA_ACCESS` hook | The AOB is in `signatures::` but we're reading stamina through the standard StatEntry pattern instead. This hook would let us verify our stamina read path against the game's own or intercept stamina writes directly | Signature known, using alternate read path |

#### Where to Look

- **Animation**: The animation system uses **.paac action chart files**, not simple actor struct fields. [CDAnimCancel](https://github.com/faisalkindi/CDAnimCancel) has a `extract_paac.py` parser and found the evaluator function at `CrimsonDesert.exe+2712090` (AOB: `0F 28 CE 48 89 4C 24 20 48 8B CB E8`). [CrimsonForge](https://www.nexusmods.com/crimsondesert/mods/446) can extract .paa animation files from PAZ
- **Quest/Cutscene**: WorldSystem (+0x30 = ActorManager) likely has sibling pointers to other managers. Scan +0x38, +0x40, +0x48 etc. [NattKh Save Editor](https://github.com/NattKh/CRIMSON-DESERT-SAVE-EDITOR) has 633 quests / 5,450 missions for validation. CDAnimCancel found InputBlock RTTI at `0x144AFCC70` handles "menu/cutscene blocking" - possible lead
- **Dragon HP**: Confirmed float type. 2026-04-18 research pass located the full mount struct field map at the dragon-timer injection point (`+0x339D8CB`) and identified `+0xD8` as the strongest HP candidate (only standalone float in the stat cluster, written with xmm8). Needs in-game read-back — see [`docs/RESEARCH_2026-04-18.md`](docs/RESEARCH_2026-04-18.md) #5
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
| **Fast-Travel Capture** | Implemented (opt-in) | `SafetyHookMid` at `+0xAB5594` reads `[r15+0x1C..0x28]`; host broadcasts `TELEPORT_TRIGGER` |
| **Mount HP / Stamina Sync** | Implemented (opt-in) | `MOUNT_PTR_CAPTURE` mid-hook + `MountSync` polls at 5Hz, broadcasts `MOUNT_STATE`; overlay shows peer's bars |
| **DX12 Present** | Implemented | Hook for ImGui overlay and frame tick |
| **Steam P2P** | Implemented | ISteamNetworkingSockets with reliable/unreliable channels |
| **40+ AOB Signatures** | Verified | Primary/fallback patterns from community mods |

### Related Projects & Offset Sources

- [CrimsonDesert-player-status-modifier](https://github.com/Orcax-1399/CrimsonDesert-player-status-modifier) - Stats, position, damage, durability signatures (ASI mod, safetyhook)
- [CDAnimCancel](https://github.com/faisalkindi/CDAnimCancel) - **Animation system RE**: .paac format parser, evaluator function AOBs, action chart structure (April 2026)
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
