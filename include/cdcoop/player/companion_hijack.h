#pragma once

#include <cstdint>
#include <cdcoop/core/game_structures.h>

namespace cdcoop {

// Hijacks a companion slot to create a player-controlled entity for player 2.
// Instead of AI-controlling the companion, we override its position, rotation,
// and animation state with data received from the remote player.
//
// This approach is chosen because:
// 1. The game already handles companion rendering, collision, and camera
// 2. Companions already have combat animations and can deal damage
// 3. It avoids the complexity of spawning a completely new entity type
class CompanionHijack {
public:
    static CompanionHijack& instance();

    // Initialize - find the companion system in memory
    bool initialize();
    void shutdown();

    // Activate: take over a companion slot for player 2
    // Returns true if a companion was successfully hijacked
    bool activate();

    // Deactivate: restore the companion to AI control
    void deactivate();

    // Get the hijacked companion's entity pointer (used by sync systems)
    uintptr_t get_entity_ptr() const { return hijacked_entity_; }
    bool is_active() const { return active_; }

    // Override companion state with remote player data
    void set_position(const Vec3& pos, const Quat& rot);
    void set_animation(uint32_t anim_id, float blend, float speed, float time);
    void set_health(float health, float max_health);

    // Disable AI controller on the hijacked companion
    void disable_ai();
    void enable_ai();

private:
    CompanionHijack() = default;

    uintptr_t companion_system_ = 0;   // Pointer to game's companion manager
    uintptr_t hijacked_entity_ = 0;    // The companion entity we took over
    uintptr_t original_ai_ctrl_ = 0;   // Saved AI controller for restoration
    int hijacked_slot_ = -1;           // Which companion slot we're using
    bool active_ = false;
};

} // namespace cdcoop
