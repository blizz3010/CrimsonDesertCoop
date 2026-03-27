#pragma once

#include <cstdint>
#include <cdcoop/core/game_structures.h>

namespace cdcoop {

// Manages the local and remote player entities
class PlayerManager {
public:
    static PlayerManager& instance();

    bool initialize();
    void shutdown();
    void update(float delta_time);

    // Local player (always the game's actual player character)
    uintptr_t local_player() const { return local_player_; }
    Vec3 local_position() const;
    Quat local_rotation() const;
    float local_health() const;

    // Remote player (the hijacked companion entity)
    uintptr_t remote_player() const;
    void spawn_remote_player();
    void despawn_remote_player();

    bool is_remote_spawned() const;

private:
    PlayerManager() = default;

    void find_local_player();

    uintptr_t local_player_ = 0;
    uintptr_t game_instance_ = 0;
};

} // namespace cdcoop
