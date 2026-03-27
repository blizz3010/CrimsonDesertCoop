#include <cdcoop/core/config.h>
#include <fstream>
#include <spdlog/spdlog.h>

namespace cdcoop {

static Config g_config;
static const std::string CONFIG_PATH = "cdcoop_config.json";

Config Config::load(const std::string& path) {
    Config cfg;
    try {
        std::ifstream f(path);
        if (f.is_open()) {
            nlohmann::json j;
            f >> j;
            cfg = j.get<Config>();
            spdlog::info("Config loaded from {}", path);
        } else {
            spdlog::info("No config file found at {} - using defaults", path);
            cfg.save(path); // Create default config file
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to load config: {} - using defaults", e.what());
    }
    return cfg;
}

void Config::save(const std::string& path) const {
    try {
        nlohmann::json j = *this;
        std::ofstream f(path);
        f << j.dump(4);
        spdlog::info("Config saved to {}", path);
    } catch (const std::exception& e) {
        spdlog::error("Failed to save config: {}", e.what());
    }
}

Config& get_config() {
    return g_config;
}

void reload_config() {
    g_config = Config::load(CONFIG_PATH);
}

} // namespace cdcoop
