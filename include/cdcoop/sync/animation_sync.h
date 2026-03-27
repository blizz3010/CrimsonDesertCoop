#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <cdcoop/core/game_structures.h>

namespace cdcoop {

// Manages animation state replication for the remote player entity.
// Uses the game's existing animation system by writing to the companion entity's
// animation controller that we hijacked for player 2.
class AnimationSync {
public:
    static AnimationSync& instance();

    void initialize();
    void shutdown();

    // Apply a received animation state to the remote player entity
    void apply_remote_animation(uintptr_t entity_ptr, uint32_t anim_id,
                                float blend_weight, float speed, float normalized_time);

    // Map animation IDs between players if they use different character models
    uint32_t remap_animation(uint32_t source_anim_id, int source_model, int target_model);

private:
    AnimationSync() = default;

    // Animation ID mapping tables (populated from game data)
    // Key: (source_model_id, anim_id) -> target_anim_id
    std::unordered_map<uint64_t, uint32_t> anim_remap_table_;
};

} // namespace cdcoop
