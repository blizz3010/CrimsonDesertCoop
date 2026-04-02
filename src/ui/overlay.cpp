#include <cdcoop/ui/overlay.h>
#include <cdcoop/network/session.h>
#include <cdcoop/player/player_manager.h>
#include <cdcoop/sync/player_sync.h>
#include <cdcoop/core/config.h>
#include <cdcoop/core/game_structures.h>
#include <spdlog/spdlog.h>

#include <imgui.h>

// Forward declarations from imgui_impl_dx12.cpp
namespace cdcoop { namespace dx12_hook {
    bool install_present_hook();
    void remove_present_hook();
    float get_delta_time();
}}

namespace cdcoop {

Overlay& Overlay::instance() {
    static Overlay inst;
    return inst;
}

bool Overlay::initialize() {
    // Hook DX12 Present to inject ImGui rendering into the game's render pipeline.
    // The hook creates a dummy DXGI device to steal the IDXGISwapChain vtable,
    // then installs an inline hook on Present (vtable index 8).
    // ImGui is initialized on the first Present call with the real device/swap chain.

    if (!dx12_hook::install_present_hook()) {
        spdlog::error("Overlay: Failed to install DX12 Present hook");
        spdlog::warn("Overlay: UI will be unavailable. Game rendering is unaffected.");
        return false;
    }

    present_hook_addr_ = 1; // Mark as hooked (actual addr is internal to dx12_hook)
    initialized_ = true;
    spdlog::info("Overlay: DX12 Present hook installed, ImGui will init on first frame");
    return true;
}

void Overlay::shutdown() {
    if (!initialized_) return;

    dx12_hook::remove_present_hook();
    initialized_ = false;
    spdlog::info("Overlay: shut down");
}

void Overlay::render() {
    if (!initialized_ || !visible_) return;

    // This is called from within the Present hook after ImGui::NewFrame()
    render_session_panel();

    if (show_debug_) {
        render_debug_panel();
    }

    render_status_bar();
}

void Overlay::toggle_visible() {
    visible_ = !visible_;
    spdlog::info("Overlay: {}", visible_ ? "shown" : "hidden");
}

void Overlay::render_session_panel() {
    ImGui::Begin("Crimson Desert Co-op", &visible_, ImGuiWindowFlags_AlwaysAutoResize);

    auto& session = Session::instance();

    switch (session.state()) {
        case SessionState::DISCONNECTED: {
            ImGui::Text("Not connected");
            ImGui::Separator();
            if (ImGui::Button("Host Session (F7)")) {
                session.host_session();
            }
            static char join_target[128] = "";
            ImGui::InputText("Steam ID / IP", join_target, sizeof(join_target));
            if (ImGui::Button("Join Session")) {
                session.join_session(join_target);
            }
            break;
        }
        case SessionState::HOSTING: {
            ImGui::Text("Hosting... Waiting for player 2");
            ImGui::Text("Share your Steam ID or invite a friend");
            if (ImGui::Button("Cancel")) {
                session.leave_session();
            }
            break;
        }
        case SessionState::CONNECTING: {
            ImGui::Text("Connecting...");
            break;
        }
        case SessionState::CONNECTED: {
            ImGui::Text("Connected to: %s", session.peer_name().c_str());
            ImGui::Text("Ping: %.0f ms", session.ping_ms());
            ImGui::Text("Role: %s", session.is_host() ? "Host" : "Client");
            ImGui::Separator();
            if (ImGui::Button("Disconnect")) {
                session.leave_session();
            }
            ImGui::SameLine();
            ImGui::Checkbox("Debug", &show_debug_);
            break;
        }
    }

    ImGui::End();
}

void Overlay::render_debug_panel() {
    ImGui::Begin("Debug Info", &show_debug_);

    auto& pm = PlayerManager::instance();
    auto local_pos = pm.local_position();
    ImGui::Text("Local Player: 0x%llX", static_cast<unsigned long long>(pm.local_player()));
    ImGui::Text("Position: %.1f, %.1f, %.1f", local_pos.x, local_pos.y, local_pos.z);
    ImGui::Text("Health: %.0f", pm.local_health());

    ImGui::Separator();

    auto& ps = PlayerSync::instance();
    auto& remote = ps.remote_state();
    ImGui::Text("Remote Position: %.1f, %.1f, %.1f",
                remote.position.x, remote.position.y, remote.position.z);
    ImGui::Text("Tether: %s", ps.is_tether_active() ? "ACTIVE" : "ok");

    auto& rt = get_runtime_offsets();
    ImGui::Separator();
    ImGui::Text("WorldSystem: 0x%llX (%s)",
                static_cast<unsigned long long>(rt.world_system_ptr),
                rt.world_system_resolved ? "OK" : "NOT FOUND");
    ImGui::Text("ActorManager: 0x%llX", static_cast<unsigned long long>(rt.actor_manager_ptr));
    ImGui::Text("PlayerActor: 0x%llX (%s)",
                static_cast<unsigned long long>(rt.player_actor_ptr),
                rt.player_resolved ? "OK" : "NOT FOUND");
    ImGui::Text("Position: %s", rt.position_resolved ? "TRACKING" : "WAITING");
    ImGui::Text("Camera: 0x%llX (%s)",
                static_cast<unsigned long long>(rt.camera_struct_ptr),
                rt.camera_resolved ? "OK" : "NOT FOUND");
    if (rt.camera_resolved) {
        float fov = read_mem<float>(rt.camera_struct_ptr, offsets::Camera::ZOOM_FOV);
        ImGui::Text("  Zoom/FOV: %.2f", fov);
    }

    ImGui::End();
}

void Overlay::render_status_bar() {
    auto& session = Session::instance();
    if (!session.is_active()) return;

    // Small status bar at the top of the screen
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::Begin("##status", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("Co-op: %s | Ping: %.0fms", session.peer_name().c_str(), session.ping_ms());
    ImGui::End();
}

} // namespace cdcoop
