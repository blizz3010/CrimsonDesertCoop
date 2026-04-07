// CrimsonDesertCoop - ASI Plugin Entry Point
// This DLL is loaded by an ASI loader (e.g., Ultimate ASI Loader) into the
// Crimson Desert game process. It bootstraps the entire co-op mod.

#include <Windows.h>
#include <thread>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <cdcoop/core/hooks.h>
#include <cdcoop/core/config.h>
#include <cdcoop/network/session.h>
#include <cdcoop/sync/player_sync.h>
#include <cdcoop/sync/enemy_sync.h>
#include <cdcoop/sync/world_sync.h>
#include <cdcoop/player/companion_hijack.h>
#include <cdcoop/player/player_manager.h>
#include <cdcoop/sync/animation_sync.h>
#include <cdcoop/ui/overlay.h>

namespace {

void setup_logging() {
    auto& cfg = cdcoop::get_config();
    auto logger = spdlog::basic_logger_mt("cdcoop", "cdcoop.log", true);
    spdlog::set_default_logger(logger);

    switch (cfg.log_level) {
        case 0: spdlog::set_level(spdlog::level::trace); break;
        case 1: spdlog::set_level(spdlog::level::debug); break;
        case 2: spdlog::set_level(spdlog::level::info); break;
        case 3: spdlog::set_level(spdlog::level::warn); break;
        case 4: spdlog::set_level(spdlog::level::err); break;
        default: spdlog::set_level(spdlog::level::info); break;
    }

    spdlog::info("=== CrimsonDesertCoop v{} ===", CDCOOP_VERSION);
    spdlog::info("Log level: {}", cfg.log_level);
}

void mod_main() {
    // Wait for the game to fully initialize
    // The game module needs to be loaded and its main structures initialized
    // before we can scan for signatures and install hooks
    Sleep(5000);

    try {
        // Load configuration
        cdcoop::reload_config();
        setup_logging();

        spdlog::info("Initializing CrimsonDesertCoop...");

        // Step 1: Initialize the hook manager (finds game module, sets up sig scanning)
        if (!cdcoop::HookManager::instance().initialize()) {
            spdlog::critical("Failed to initialize hook manager!");
            return;
        }
        spdlog::info("Hook manager initialized. Game base: 0x{:X}",
                      cdcoop::HookManager::instance().game_base());

        // Step 2: Initialize the player manager (finds player entity in memory)
        if (!cdcoop::PlayerManager::instance().initialize()) {
            spdlog::critical("Failed to initialize player manager!");
            return;
        }
        spdlog::info("Player manager initialized");

        // Step 3: Initialize sync systems
        cdcoop::PlayerSync::instance().initialize();
        cdcoop::EnemySync::instance().initialize();
        cdcoop::WorldSync::instance().initialize();
        cdcoop::AnimationSync::instance().initialize();
        spdlog::info("Sync systems initialized");

        // Step 4: Initialize companion hijack system
        if (!cdcoop::CompanionHijack::instance().initialize()) {
            spdlog::warn("Companion hijack init failed - will retry when companion spawns");
        }

        // Step 5: Initialize overlay UI
        if (!cdcoop::Overlay::instance().initialize()) {
            spdlog::warn("Overlay initialization failed - UI will be unavailable");
        }

        spdlog::info("CrimsonDesertCoop fully initialized! Press F8 for overlay, F7 to host/join.");
        spdlog::info("Waiting for session...");

    } catch (const std::exception& e) {
        spdlog::critical("Fatal error during initialization: {}", e.what());
    }
}

} // anonymous namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // Launch initialization on a separate thread to avoid blocking DllMain
        std::thread(mod_main).detach();
    } else if (reason == DLL_PROCESS_DETACH) {
        spdlog::info("CrimsonDesertCoop shutting down...");
        cdcoop::Overlay::instance().shutdown();
        cdcoop::CompanionHijack::instance().shutdown();
        cdcoop::WorldSync::instance().shutdown();
        cdcoop::EnemySync::instance().shutdown();
        cdcoop::PlayerSync::instance().shutdown();
        cdcoop::PlayerManager::instance().shutdown();
        cdcoop::Session::instance().leave_session();
        cdcoop::HookManager::instance().shutdown();
        spdlog::info("Shutdown complete.");
        spdlog::shutdown();
    }
    return TRUE;
}
