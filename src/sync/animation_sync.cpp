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

    // Animation remap table is empty for now - using passthrough mode.
    // Real animation IDs need to be extracted from PAZ archives before
    // cross-model remapping can work. For MVP, we pass through the raw
    // anim ID from the remote player, which works when both players
    // use the same character model.
    spdlog::info("AnimationSync: using passthrough mode (real anim IDs not yet extracted from PAZ)");
}

void AnimationSync::shutdown() {
    anim_remap_table_.clear();
}

void AnimationSync::apply_remote_animation(uintptr_t entity_ptr, uint32_t anim_id,
                                            float blend_weight, float speed,
                                            float normalized_time) {
    if (!is_valid_ptr(entity_ptr)) return;

    // Write animation state directly to the entity's animation fields.
    // The actor layout places animation state at offset 0x120 and blend at 0x124.
    write_mem<uint32_t>(entity_ptr, offsets::Companion::ANIM_STATE, anim_id);
    write_mem<float>(entity_ptr, offsets::Player::ANIM_BLEND, blend_weight);
}

uint32_t AnimationSync::remap_animation(uint32_t source_anim_id, int source_model,
                                         int target_model) {
    // Passthrough mode: always use the source anim ID directly.
    // Cross-model remapping requires real animation IDs extracted from PAZ archives.
    // For now, this works correctly when source_model == target_model.
    return source_anim_id;
}

} // namespace cdcoop
