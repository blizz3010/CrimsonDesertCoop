#include <cdcoop/ui/overlay.h>
#include <cdcoop/network/session.h>
#include <cdcoop/player/player_manager.h>
#include <cdcoop/sync/player_sync.h>
#include <cdcoop/core/config.h>
#include <spdlog/spdlog.h>

// ImGui includes
// #include <imgui.h>
// #include <imgui_impl_win32.h>
// #include <imgui_impl_dx12.h>

namespace cdcoop {

Overlay& Overlay::instance() {
    static Overlay inst;
    return inst;
}

bool Overlay::initialize() {
    // To render an overlay in Crimson Desert, we need to hook into its
    // DirectX 12 rendering pipeline. The BlackSpace Engine uses DX12.
    //
    // Approach:
    // 1. Find IDXGISwapChain::Present via vtable hooking or signature scan
    // 2. Hook Present to inject our ImGui rendering after the game renders
    // 3. Initialize ImGui with DX12 backend
    //
    // This is a well-known technique used by many game overlays (MSI Afterburner,
    // Steam overlay, etc.)

    // TODO: implement DX12 Present hook
    // Step 1: Get the swap chain (scan for CreateSwapChain or find via DXGI factory)
    // Step 2: Hook Present (vtable index 8 in IDXGISwapChain)
    // Step 3: In our Present hook, initialize ImGui on first call, then render

    spdlog::info("Overlay: DX12 hook setup is stubbed - implement with imgui_impl_dx12");
    initialized_ = true;
    return true;
}

void Overlay::shutdown() {
    if (!initialized_) return;

    // ImGui::DestroyContext();
    initialized_ = false;
}

void Overlay::render() {
    if (!initialized_ || !visible_) return;

    // This would be called from within our hooked Present function
    // ImGui_ImplDX12_NewFrame();
    // ImGui_ImplWin32_NewFrame();
    // ImGui::NewFrame();

    render_session_panel();

    if (show_debug_) {
        render_debug_panel();
    }

    render_status_bar();

    // ImGui::Render();
    // ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), ...);
}

void Overlay::toggle_visible() {
    visible_ = !visible_;
    spdlog::info("Overlay: {}", visible_ ? "shown" : "hidden");
}

void Overlay::render_session_panel() {
    // ImGui::Begin("Crimson Desert Co-op", &visible_, ImGuiWindowFlags_AlwaysAutoResize);

    auto& session = Session::instance();

    switch (session.state()) {
        case SessionState::DISCONNECTED: {
            // ImGui::Text("Not connected");
            // ImGui::Separator();
            // if (ImGui::Button("Host Session (F7)")) {
            //     session.host_session();
            // }
            // static char join_target[128] = "";
            // ImGui::InputText("Steam ID / IP", join_target, sizeof(join_target));
            // if (ImGui::Button("Join Session")) {
            //     session.join_session(join_target);
            // }
            break;
        }
        case SessionState::HOSTING: {
            // ImGui::Text("Hosting... Waiting for player 2");
            // ImGui::Text("Share your Steam ID or invite a friend");
            // if (ImGui::Button("Cancel")) {
            //     session.leave_session();
            // }
            break;
        }
        case SessionState::CONNECTING: {
            // ImGui::Text("Connecting...");
            break;
        }
        case SessionState::CONNECTED: {
            // ImGui::Text("Connected to: %s", session.peer_name().c_str());
            // ImGui::Text("Ping: %.0f ms", session.ping_ms());
            // ImGui::Text("Role: %s", session.is_host() ? "Host" : "Client");
            // ImGui::Separator();
            // if (ImGui::Button("Disconnect")) {
            //     session.leave_session();
            // }
            break;
        }
    }

    // ImGui::End();
}

void Overlay::render_debug_panel() {
    // ImGui::Begin("Debug Info");

    auto& pm = PlayerManager::instance();
    auto local_pos = pm.local_position();
    // ImGui::Text("Local Player: 0x%llX", pm.local_player());
    // ImGui::Text("Position: %.1f, %.1f, %.1f", local_pos.x, local_pos.y, local_pos.z);

    auto& ps = PlayerSync::instance();
    auto& remote = ps.remote_state();
    // ImGui::Text("Remote Position: %.1f, %.1f, %.1f",
    //             remote.position.x, remote.position.y, remote.position.z);
    // ImGui::Text("Tether: %s", ps.is_tether_active() ? "ACTIVE" : "ok");

    // ImGui::End();
}

void Overlay::render_status_bar() {
    auto& session = Session::instance();
    if (!session.is_active()) return;

    // Small status bar at the top of the screen
    // ImGui::SetNextWindowPos(ImVec2(10, 10));
    // ImGui::Begin("##status", nullptr,
    //     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
    //     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    // ImGui::Text("Co-op: %s | Ping: %.0fms", session.peer_name().c_str(), session.ping_ms());
    // ImGui::End();
}

} // namespace cdcoop
