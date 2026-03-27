#pragma once

#include <cstdint>
#include <unordered_map>
#include <cdcoop/core/game_structures.h>
#include <cdcoop/network/packet.h>

namespace cdcoop {

// Syncs enemy state between host and client.
// Host is AUTHORITATIVE for all enemy state.
// Client sends damage events to host, host validates and broadcasts results.
class EnemySync {
public:
    static EnemySync& instance();

    void initialize();
    void shutdown();
    void update(float delta_time);

    // Host: broadcast enemy state changes
    void on_enemy_state_changed(uint32_t entity_id, const Vec3& pos, const Quat& rot,
                                float health, uint32_t state);
    void on_enemy_death(uint32_t entity_id);
    void on_enemy_spawn(uint32_t entity_id, const Vec3& pos, uint32_t type_id);

    // Client: report damage dealt to an enemy
    void report_damage(uint32_t entity_id, float damage);

    // Apply HP scaling for co-op (called when session connects)
    void apply_coop_scaling();
    void revert_coop_scaling();

private:
    EnemySync() = default;

    void on_remote_enemy_state(const uint8_t* data, size_t size);
    void on_remote_enemy_damage(const uint8_t* data, size_t size);
    void on_remote_enemy_death(const uint8_t* data, size_t size);

    // Track enemy original HP for scaling revert
    struct EnemyOriginalStats {
        float max_health;
    };
    std::unordered_map<uint32_t, EnemyOriginalStats> original_stats_;

    float send_timer_ = 0.0f;
    static constexpr float ENEMY_SYNC_RATE = 1.0f / 15.0f; // 15 Hz
};

} // namespace cdcoop
