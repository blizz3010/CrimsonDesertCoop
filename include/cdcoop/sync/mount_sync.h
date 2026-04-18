#pragma once

#include <cstdint>
#include <cdcoop/core/game_structures.h>
#include <cdcoop/network/packet.h>

namespace cdcoop {

// Syncs mount state (HP / stamina / mount type) between the two peers.
// Each peer broadcasts their own mount's state — there's no authoritative
// owner because in our architecture both players run local games and
// each mounts their own entity (the companion hijack does not drive
// mount animation yet).
//
// Scope of this first cut:
//   * Mount pointer is captured by the Orcax MOUNT_PTR_CAPTURE mid-hook
//     (see include/cdcoop/core/hooks.h::mount_ptr_capture_hook).
//   * While captured, the local side reads HP/stamina at 5Hz from the
//     mount's stats component (same +0x58 StatEntry layout as player).
//   * State is broadcast as MOUNT_STATE packet and displayed in the
//     overlay. Visual sync (spawning a mount entity next to the
//     companion) is out of scope — the companion entity still appears
//     to hover at mount height on the remote side.
class MountSync {
public:
    static MountSync& instance();

    void initialize();
    void shutdown();
    void update(float delta_time);

    struct MountView {
        bool  is_mounted      = false;
        float health          = 0.0f;
        float max_health      = 0.0f;
        float stamina         = 0.0f;
        float max_stamina     = 0.0f;
        uint32_t mount_entity_id = 0;
        uint32_t mount_type_hash = 0;
    };

    const MountView& remote_state() const { return remote_; }
    const MountView& local_state()  const { return local_; }

private:
    MountSync() = default;

    void on_remote_mount_state(const uint8_t* data, size_t size);

    // Read HP/stamina from the captured mount pointer. Returns false if
    // the pointer is no longer valid (player dismounted).
    bool poll_local_mount(MountView& out);

    // Broadcast our local mount view. Unreliable (high-rate telemetry).
    void broadcast(const MountView& view);

    MountView local_{};
    MountView remote_{};
    bool      last_broadcast_mounted_ = false;

    float send_timer_ = 0.0f;
    static constexpr float MOUNT_SYNC_RATE = 1.0f / 5.0f; // 5 Hz
};

} // namespace cdcoop
