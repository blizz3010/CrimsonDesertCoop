#pragma once

#include <cstdint>

namespace cdcoop {

// ImGui-based overlay for co-op session management and debug info.
// Hooks into the game's DirectX 12 present call to render UI on top.
class Overlay {
public:
    static Overlay& instance();

    bool initialize();
    void shutdown();

    void render();
    void toggle_visible();
    bool is_visible() const { return visible_; }

private:
    Overlay() = default;

    void render_session_panel();
    void render_debug_panel();
    void render_status_bar();

    bool initialized_ = false;
    bool visible_ = false;
    bool show_debug_ = false;

    // DX12 hook state
    uintptr_t present_hook_addr_ = 0;
};

} // namespace cdcoop
