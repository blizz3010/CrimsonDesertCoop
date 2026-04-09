#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace cdcoop {

struct Config {
    // Networking
    std::string player_name = "Player";
    uint16_t port = 27015;
    bool use_steam_networking = true;

    // Gameplay
    float enemy_hp_multiplier = 1.5f;   // Scale enemy HP for 2 players
    float enemy_dmg_multiplier = 1.0f;  // Scale enemy damage
    float tether_distance = 150.0f;     // Max distance between players (meters)
    bool sync_cutscenes = false;       // Not yet implemented (awaiting quest/cutscene manager offsets)
    bool sync_quest_progress = false;  // Not yet implemented (awaiting quest manager offsets)
    bool skip_animation_remap = true;    // passthrough mode - no cross-model remap

    // Player 2 appearance
    int player2_model_id = -1; // -1 = same as host, otherwise specific model ID
    bool player2_use_companion_slot = true; // hijack companion vs spawn new entity

    // Debug
    bool debug_overlay = false;
    bool log_packets = false;
    int log_level = 2; // 0=trace, 1=debug, 2=info, 3=warn, 4=error

    // Keybinds
    int toggle_overlay_key = 0x77; // F8
    int open_session_key = 0x76;   // F7

    // Load from / save to JSON file
    static Config load(const std::string& path);
    void save(const std::string& path) const;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Config,
        player_name, port, use_steam_networking,
        enemy_hp_multiplier, enemy_dmg_multiplier, tether_distance,
        sync_cutscenes, sync_quest_progress, skip_animation_remap,
        player2_model_id, player2_use_companion_slot,
        debug_overlay, log_packets, log_level,
        toggle_overlay_key, open_session_key
    )
};

Config& get_config();
void reload_config();

} // namespace cdcoop
