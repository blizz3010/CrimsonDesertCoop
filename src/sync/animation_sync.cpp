#include <cdcoop/sync/animation_sync.h>
#include <cdcoop/core/game_structures.h>
#include <spdlog/spdlog.h>

namespace cdcoop {

AnimationSync& AnimationSync::instance() {
    static AnimationSync inst;
    return inst;
}

void AnimationSync::initialize() {
    spdlog::info("AnimationSync initialized");

    // TODO: populate animation remap table from game data
    // This is needed if player 2 uses a different character model
    // than the host. For now, we assume same model = same anim IDs.
}

void AnimationSync::shutdown() {
    anim_remap_table_.clear();
}

void AnimationSync::apply_remote_animation(uintptr_t entity_ptr, uint32_t anim_id,
                                            float blend_weight, float speed,
                                            float normalized_time) {
    if (entity_ptr == 0) return;

    // Write animation state directly to the entity's animation controller
    // This bypasses the AI system and forces the companion to play
    // whatever animation the remote player is playing

    // Write animation state directly to the entity's animation fields.
    // The actor layout places animation state at offset 0x120 and blend at 0x124
    // (same layout for player and companion actors in the BlackSpace Engine).
    write_mem<uint32_t>(entity_ptr, offsets::Companion::ANIM_STATE, anim_id);
    write_mem<float>(entity_ptr, offsets::Player::ANIM_BLEND, blend_weight);
}

uint32_t AnimationSync::remap_animation(uint32_t source_anim_id, int source_model,
                                         int target_model) {
    if (source_model == target_model) return source_anim_id;

    uint64_t key = (static_cast<uint64_t>(source_model) << 32) | source_anim_id;
    auto it = anim_remap_table_.find(key);
    if (it != anim_remap_table_.end()) {
        return it->second;
    }

    // No mapping found - use the same ID and hope for the best
    spdlog::warn("No animation remap for model {}->{}  anim {}", source_model, target_model, source_anim_id);
    return source_anim_id;
}

} // namespace cdcoop
