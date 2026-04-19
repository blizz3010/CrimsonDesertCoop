// CrimsonDesertCoop - ASI Plugin Entry Point
// This DLL is loaded by an ASI loader (e.g., Ultimate ASI Loader) into the
// Crimson Desert game process. It bootstraps the entire co-op mod.

#include <Windows.h>
#include <thread>
#include <atomic>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <cdcoop/core/hooks.h>
#include <cdcoop/core/config.h>
#include <cdcoop/core/memory.h>
#include <cdcoop/network/session.h>
#include <cdcoop/network/steam_network.h>
#include <cdcoop/sync/player_sync.h>
#include <cdcoop/sync/enemy_sync.h>
#include <cdcoop/sync/world_sync.h>
#include <cdcoop/sync/mount_sync.h>
#include <cdcoop/player/companion_hijack.h>
#include <cdcoop/player/player_manager.h>
#include <cdcoop/sync/animation_sync.h>
#include <cdcoop/ui/overlay.h>

// MSVC linker-provided symbol: address = HMODULE of the containing DLL.
// Lets marker helpers find the DLL's path without threading hModule
// through from DllMain.
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

// Appends a timestamped line to cdcoop_loaded.txt next to the DLL. Plain
// WinAPI — never touches spdlog — so it works even if the logger failed
// to open cdcoop.log (read-only install dir, antivirus, etc.). The marker
// file becomes a low-fidelity init journal that pinpoints how far we got
// when users report "the mod isn't working". Wide-path to survive
// non-ASCII install dirs (matches drop_load_marker).
void append_marker(const char* msg) {
    wchar_t wpath[MAX_PATH];
    DWORD len = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase),
                                   wpath, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return;
    std::wstring path(wpath, len);
    auto slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) return;
    path.resize(slash + 1);
    path += L"cdcoop_loaded.txt";

    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, nullptr, FILE_END);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[512];
    int n = wsprintfA(buf, "[%02d:%02d:%02d] %s\r\n",
                      st.wHour, st.wMinute, st.wSecond, msg);
    DWORD written = 0;
    if (n > 0) WriteFile(h, buf, static_cast<DWORD>(n), &written, nullptr);
    CloseHandle(h);
}

// Hotkey input thread. Runs independently of the DX12 Present hook so
// F7 / F8 keep working even when the overlay fails to attach (e.g. when
// another mod hooked DXGI first — Optiscaler, ReShade, XeSS wrappers,
// or a driver-level overlay on non-NVIDIA GPUs). Without this fallback
// the hotkeys are a no-op whenever the rendering hook chain breaks.
std::atomic<bool> g_input_thread_exit{false};
std::thread g_input_thread;

// Fallback tick thread. The Present hook is the normal driver of
// Session::update + PlayerSync/EnemySync/WorldSync/MountSync/PlayerManager
// update, which means packet polling, heartbeats, and timeout detection
// all depend on DXGI Present being hooked. When that hook fails to
// install (same conditions that kill the overlay), a session can be
// created via F7 but never actually exchange a packet — the peer never
// sees the handshake and the session looks dead. This thread runs the
// same update chain at ~60Hz *only* when the Present hook didn't take
// over, so Evan's "no overlay, no friends, mod looks dead" path finally
// has a heartbeat.
std::atomic<bool> g_fallback_tick_exit{false};
std::thread g_fallback_tick_thread;

void input_poll_loop() {
    const auto& cfg = cdcoop::get_config();
    const int host_key   = cfg.open_session_key;
    const int toggle_key = cfg.toggle_overlay_key;

    spdlog::info("Input thread started (host={:#x}, overlay={:#x}). "
                 "Hotkeys will work regardless of overlay state.",
                 host_key, toggle_key);

    bool host_prev = false;
    bool toggle_prev = false;

    while (!g_input_thread_exit.load(std::memory_order_relaxed)) {
        // GetAsyncKeyState high bit = currently held. Edge-detect
        // transition-to-pressed so holding the key doesn't re-fire.
        bool host_now = (GetAsyncKeyState(host_key) & 0x8000) != 0;
        if (host_now && !host_prev) {
            auto& session = cdcoop::Session::instance();
            if (session.state() == cdcoop::SessionState::DISCONNECTED) {
                spdlog::info("Host hotkey: starting session");
                session.host_session();
            } else {
                spdlog::info("Host hotkey: session already active (state {})",
                             static_cast<int>(session.state()));
            }
        }
        host_prev = host_now;

        bool toggle_now = (GetAsyncKeyState(toggle_key) & 0x8000) != 0;
        if (toggle_now && !toggle_prev) {
            cdcoop::Overlay::instance().toggle_visible();
            spdlog::debug("Overlay toggle hotkey fired");
        }
        toggle_prev = toggle_now;

        Sleep(16); // ~60Hz polling — imperceptible input latency, trivial CPU
    }

    spdlog::info("Input thread exited");
}

// Mirrors the update chain driven by the DX12 Present hook in
// src/ui/imgui_impl_dx12.cpp. Only started when the Present hook
// failed to install, so the two tickers never race.
void fallback_tick_loop() {
    LARGE_INTEGER freq, last;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);

    spdlog::info("Fallback tick thread started — Present hook unavailable, "
                 "driving session/sync updates from here at ~60Hz");

    while (!g_fallback_tick_exit.load(std::memory_order_relaxed)) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = static_cast<float>(now.QuadPart - last.QuadPart) /
                   static_cast<float>(freq.QuadPart);
        last = now;
        if (dt > 0.1f) dt = 0.1f; // clamp to match Present-path behavior

        auto& session = cdcoop::Session::instance();
        if (session.is_active()) {
            session.update(dt);
            cdcoop::PlayerSync::instance().update(dt);
            cdcoop::EnemySync::instance().update(dt);
            cdcoop::WorldSync::instance().update(dt);
        }
        cdcoop::MountSync::instance().update(dt);
        cdcoop::PlayerManager::instance().update(dt);

        Sleep(16);
    }

    spdlog::info("Fallback tick thread exited");
}

void setup_logging() {
    auto& cfg = cdcoop::get_config();
    // Anchor the log at the DLL's own directory. The old relative path
    // resolved against the game process's CWD, which differs between
    // Steam launches, Proton, and various ASI loaders — users routinely
    // couldn't find cdcoop.log because it landed somewhere unexpected.
    std::string log_path = cdcoop::self_module_dir() + "cdcoop.log";
    auto logger = spdlog::basic_logger_mt("cdcoop", log_path, true);
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
    append_marker("mod_main entered (post-5s wait)");

    try {
        // Load configuration
        cdcoop::reload_config();
        append_marker("config loaded");
        setup_logging();
        append_marker("logging started (see cdcoop.log from here on)");

        spdlog::info("Initializing CrimsonDesertCoop...");

        // Step 1: Initialize the hook manager (finds game module, sets up sig scanning)
        if (!cdcoop::HookManager::instance().initialize()) {
            spdlog::critical("Failed to initialize hook manager!");
            append_marker("FAIL: hook manager init returned false");
            return;
        }
        spdlog::info("Hook manager initialized. Game base: 0x{:X}",
                      cdcoop::HookManager::instance().game_base());
        append_marker("hook manager initialized");

        // Step 2: Initialize the player manager (finds player entity in memory)
        if (!cdcoop::PlayerManager::instance().initialize()) {
            spdlog::critical("Failed to initialize player manager!");
            append_marker("FAIL: player manager init returned false");
            return;
        }
        spdlog::info("Player manager initialized");
        append_marker("player manager initialized");

        // Step 3: Initialize sync systems
        cdcoop::PlayerSync::instance().initialize();
        cdcoop::EnemySync::instance().initialize();
        cdcoop::WorldSync::instance().initialize();
        cdcoop::MountSync::instance().initialize();
        cdcoop::AnimationSync::instance().initialize();
        spdlog::info("Sync systems initialized");
        append_marker("sync systems initialized");

        // Step 4: Initialize companion hijack system
        if (!cdcoop::CompanionHijack::instance().initialize()) {
            // CompanionHijack::activate() lazily re-initializes if needed,
            // so a failure here just means we have no companion *yet* —
            // PlayerManager::spawn_remote_player() will retry when the
            // session connects and a companion is in the party.
            spdlog::warn("Companion hijack init failed - will retry on activate()");
        }

        // Step 5: Initialize overlay UI. This hooks DXGI Present and
        // can fail when another mod has already hooked it (Optiscaler,
        // ReShade, driver overlays). Non-fatal — the hotkey thread
        // below makes sure F7/F8 still work in that case.
        const bool overlay_ok = cdcoop::Overlay::instance().initialize();
        if (!overlay_ok) {
            spdlog::warn("Overlay initialization failed — the ImGui UI will not render. "
                         "F7/F8 still work via the input thread; share Steam IDs manually.");
        }

        // Step 6: Start the fallback input thread. Must run even when
        // the overlay is fine, because it's also the source of truth
        // for hotkey handling now (de-duplicated with the Present hook).
        g_input_thread = std::thread(input_poll_loop);

        // Step 6b: If the Present hook failed, nothing will drive
        // Session::update / sync updates / transport->poll(), so a
        // session created via F7 would connect but never exchange
        // packets. Spin up the fallback tick thread to cover that gap.
        if (!overlay_ok) {
            g_fallback_tick_thread = std::thread(fallback_tick_loop);
            append_marker("fallback tick thread started (overlay off)");
        }

        // Step 7: Install the persistent Steam invite listener so a
        // friend's "Accept Invite" / "Join Game" click spins up a
        // session even if the user hasn't opened the overlay yet.
#if CDCOOP_STEAM
        if (cdcoop::get_config().use_steam_networking) {
            cdcoop::install_steam_invite_listener();
        }
#endif

        spdlog::info("CrimsonDesertCoop fully initialized!");
        spdlog::info("  Overlay: {}", overlay_ok ? "ON (F8 toggles)" : "OFF (Present hook failed)");
        spdlog::info("  F7 = host session, F8 = toggle overlay");
        spdlog::info("Waiting for session...");
        append_marker(overlay_ok
            ? "fully initialized — overlay ON, F7 to host / F8 to toggle"
            : "fully initialized — overlay OFF (Present hook failed), F7 still hosts");

    } catch (const std::exception& e) {
        spdlog::critical("Fatal error during initialization: {}", e.what());
        // Mirror into the marker so the failure is visible even if spdlog
        // never opened cdcoop.log (the whole reason the marker exists).
        char buf[512];
        wsprintfA(buf, "FATAL: %s", e.what());
        append_marker(buf);
    }
}

} // anonymous namespace

// Drops a small marker file next to the DLL the instant DllMain fires.
// If this file is missing, the ASI loader never touched us — which is a
// distinct failure mode from "loaded but crashed before setup_logging()".
// Kept to plain WinAPI so it works even if the CRT init is incomplete.
static void drop_load_marker(HMODULE hModule) {
    wchar_t wpath[MAX_PATH];
    DWORD len = GetModuleFileNameW(hModule, wpath, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return;
    std::wstring marker(wpath, len);
    auto slash = marker.find_last_of(L'\\');
    if (slash == std::wstring::npos) return;
    marker.resize(slash + 1);
    marker += L"cdcoop_loaded.txt";

    HANDLE h = CreateFileW(marker.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[256];
    int n = wsprintfA(buf,
        "CrimsonDesertCoop ASI loaded.\r\n"
        "Local time: %04d-%02d-%02d %02d:%02d:%02d\r\n"
        "PID: %lu\r\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        GetCurrentProcessId());
    DWORD written = 0;
    if (n > 0) WriteFile(h, buf, static_cast<DWORD>(n), &written, nullptr);
    CloseHandle(h);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        drop_load_marker(hModule);
        // Launch initialization on a separate thread to avoid blocking DllMain
        std::thread(mod_main).detach();
    } else if (reason == DLL_PROCESS_DETACH) {
        spdlog::info("CrimsonDesertCoop shutting down...");

        // Stop worker threads before anything else so they can't observe
        // half-torn-down singletons during their next tick.
        g_input_thread_exit.store(true, std::memory_order_relaxed);
        g_fallback_tick_exit.store(true, std::memory_order_relaxed);
        if (g_input_thread.joinable()) {
            g_input_thread.join();
        }
        if (g_fallback_tick_thread.joinable()) {
            g_fallback_tick_thread.join();
        }

#if CDCOOP_STEAM
        cdcoop::remove_steam_invite_listener();
#endif
        cdcoop::Overlay::instance().shutdown();
        cdcoop::CompanionHijack::instance().shutdown();
        cdcoop::MountSync::instance().shutdown();
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
