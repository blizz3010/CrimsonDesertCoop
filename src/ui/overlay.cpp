#include <cdcoop/ui/overlay.h>
#include <cdcoop/network/session.h>
#include <cdcoop/player/player_manager.h>
#include <cdcoop/sync/player_sync.h>
#include <cdcoop/sync/mount_sync.h>
#include <cdcoop/core/config.h>
#include <cdcoop/core/game_structures.h>
#include <cdcoop/core/hooks.h>
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
            if (ImGui::Button("Invite Friend (Steam)")) {
                session.invite_friend();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                session.leave_session();
            }
            ImGui::TextDisabled("Tip: the friend can also right-click your name "
                                "in Steam and pick \"Join Game\".");
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

            // Peer mount HP / stamina — only shown when the peer is
            // actually mounted, to avoid cluttering the panel.
            const auto& remote_mount = MountSync::instance().remote_state();
            if (remote_mount.is_mounted) {
                ImGui::Separator();
                ImGui::Text("Peer's Mount");
                char label[48];
                if (remote_mount.max_health > 0) {
                    snprintf(label, sizeof(label), "HP %.0f / %.0f",
                             remote_mount.health, remote_mount.max_health);
                    ImGui::ProgressBar(remote_mount.health / remote_mount.max_health,
                                       ImVec2(200, 0), label);
                }
                if (remote_mount.max_stamina > 0) {
                    snprintf(label, sizeof(label), "ST %.0f / %.0f",
                             remote_mount.stamina, remote_mount.max_stamina);
                    ImGui::ProgressBar(remote_mount.stamina / remote_mount.max_stamina,
                                       ImVec2(200, 0), label);
                }
            }

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
    if (rt.camera_resolved && is_valid_ptr(rt.camera_struct_ptr)) {
        float fov = read_mem<float>(rt.camera_struct_ptr, offsets::Camera::ZOOM_FOV);
        ImGui::Text("  Zoom/FOV: %.2f", fov);
    }

    // --- Experimental / research state ---
    ImGui::Separator();
    ImGui::Text("AnimEvaluator: 0x%llX (%s)",
                static_cast<unsigned long long>(rt.animation_evaluator_ptr),
                rt.animation_evaluator_resolved ? "CAPTURED" : "not captured");
    if (rt.dragon_hp_resolved && is_valid_ptr(rt.dragon_marker_ptr)) {
        ImGui::Text("Dragon HP: marker+0x%X = %.0f",
                    rt.dragon_hp_offset,
                    read_mem<float>(rt.dragon_marker_ptr, rt.dragon_hp_offset));
    } else {
        ImGui::Text("Dragon HP: not resolved (mount a dragon first)");
    }

    const auto& local_mount  = MountSync::instance().local_state();
    const auto& remote_mount = MountSync::instance().remote_state();
    if (rt.mount_resolved && is_valid_ptr(rt.mount_ptr)) {
        ImGui::Text("Local mount: 0x%llX (%s)",
                    static_cast<unsigned long long>(rt.mount_ptr),
                    local_mount.is_mounted ? "MOUNTED" : "dismounted");
        if (local_mount.is_mounted) {
            ImGui::Text("  HP %.0f/%.0f  ST %.0f/%.0f",
                        local_mount.health, local_mount.max_health,
                        local_mount.stamina, local_mount.max_stamina);
        }
    } else {
        ImGui::Text("Mount: not captured (mount something first)");
    }
    if (remote_mount.is_mounted) {
        ImGui::Text("Remote mount: type=0x%X HP %.0f/%.0f  ST %.0f/%.0f",
                    remote_mount.mount_type_hash,
                    remote_mount.health, remote_mount.max_health,
                    remote_mount.stamina, remote_mount.max_stamina);
    }
    if (rt.world_probe_ran) {
        ImGui::Text("WS probe: ran (see cdcoop_world_probe.log)");
    } else {
        ImGui::Text("WS probe: not run (set dump_world_system_probe=true)");
    }

    // --- Hook installation telemetry ---
    ImGui::Separator();
    const auto& hs = HookManager::instance().status();
    ImGui::Text("Hooks: %d installed, %d failed", hs.installed, hs.failed);
    if (!hs.failed_names.empty()) {
        if (ImGui::TreeNode("Failed hooks")) {
            for (const auto& name : hs.failed_names) {
                ImGui::BulletText("%s", name.c_str());
            }
            ImGui::TreePop();
        }
    }
    if (!hs.installed_names.empty()) {
        if (ImGui::TreeNode("Installed hooks")) {
            for (const auto& name : hs.installed_names) {
                ImGui::BulletText("%s", name.c_str());
            }
            ImGui::TreePop();
        }
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
