#pragma once

#include <cdcoop/core/game_structures.h>
#include <cdcoop/network/packet.h>

namespace cdcoop {

// Handles synchronizing player state between host and client.
// The host's player state is authoritative for world interactions.
// Each player's own position/animation is authoritative for their character.
class PlayerSync {
public:
    static PlayerSync& instance();

    void initialize();
    void shutdown();

    // Called every game tick
    void update(float delta_time);

    // Get the remote player's interpolated state (for rendering player 2)
    const PlayerFullStatePacket& remote_state() const { return interpolated_state_; }

    // Local player state changed - broadcast to peer
    void on_local_position_changed(const Vec3& pos, const Quat& rot, const Vec3& vel, uint8_t flags);
    void on_local_animation_changed(uint32_t anim_id, float blend, float speed, float time);
    void on_local_combat_action(uint8_t action, uint32_t skill_id, const Vec3& target_pos, uint32_t target_id);

    // Tether system - keeps players within a reasonable distance
    bool is_tether_active() const { return tether_active_; }
    Vec3 get_tether_pull_direction() const;

private:
    PlayerSync() = default;

    void on_remote_position(const uint8_t* data, size_t size);
    void on_remote_animation(const uint8_t* data, size_t size);
    void on_remote_combat(const uint8_t* data, size_t size);
    void on_remote_full_state(const uint8_t* data, size_t size);

    // Interpolation for smooth remote player movement
    void interpolate_remote_state(float delta_time);

    // State buffer for interpolation (ring buffer of recent states)
    static constexpr int STATE_BUFFER_SIZE = 32;
    PlayerFullStatePacket state_buffer_[STATE_BUFFER_SIZE] = {};
    int state_buffer_head_ = 0;
    int state_buffer_count_ = 0;

    PlayerFullStatePacket interpolated_state_ = {};
    float interpolation_time_ = 0.0f;

    // Tether
    bool tether_active_ = false;
    float tether_distance_sq_ = 0.0f;

    // Rate limiting
    float send_timer_ = 0.0f;
    static constexpr float POSITION_SEND_RATE = 1.0f / 30.0f; // 30 Hz
    static constexpr float FULL_STATE_RATE = 1.0f / 5.0f;     // 5 Hz (fallback sync)
    float full_state_timer_ = 0.0f;
};

} // namespace cdcoop
