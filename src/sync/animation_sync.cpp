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

    // Populate animation remap table for cross-character model animation mapping.
    // Crimson Desert has 3 playable protagonists: Khan (0), Mara (1), Daga (2).
    // Each has unique combat animations but shared locomotion animations.
    //
    // When player 2 controls a companion whose model differs from the host's
    // character, we remap animation IDs so the correct animations play.
    //
    // Locomotion animations (shared across all models - no remap needed):
    //   Idle, Walk, Run, Sprint, Jump, Fall, Land, Climb, Swim
    //
    // Combat animations (character-specific - need remap):
    //   Light attack chains, heavy attacks, skills, dodges, parries
    //
    // The actual animation IDs need to be extracted from the game's animation
    // data (in PAZ archives). For now, we register known ranges.
    // Animation ID ranges are approximate and need verification.

    // Khan (model 0) -> Mara (model 1) combat anim remaps
    // Light attack chain: Khan 1000-1005 -> Mara 2000-2005
    for (int i = 0; i <= 5; i++) {
        uint64_t key_0_to_1 = (static_cast<uint64_t>(0) << 32) | (1000 + i);
        anim_remap_table_[key_0_to_1] = 2000 + i;
        uint64_t key_1_to_0 = (static_cast<uint64_t>(1) << 32) | (2000 + i);
        anim_remap_table_[key_1_to_0] = 1000 + i;
    }

    // Khan (model 0) -> Daga (model 2) combat anim remaps
    for (int i = 0; i <= 5; i++) {
        uint64_t key_0_to_2 = (static_cast<uint64_t>(0) << 32) | (1000 + i);
        anim_remap_table_[key_0_to_2] = 3000 + i;
        uint64_t key_2_to_0 = (static_cast<uint64_t>(2) << 32) | (3000 + i);
        anim_remap_table_[key_2_to_0] = 1000 + i;
    }

    // Mara (model 1) -> Daga (model 2)
    for (int i = 0; i <= 5; i++) {
        uint64_t key_1_to_2 = (static_cast<uint64_t>(1) << 32) | (2000 + i);
        anim_remap_table_[key_1_to_2] = 3000 + i;
        uint64_t key_2_to_1 = (static_cast<uint64_t>(2) << 32) | (3000 + i);
        anim_remap_table_[key_2_to_1] = 2000 + i;
    }

    spdlog::info("AnimationSync: registered {} remap entries (placeholder IDs)", anim_remap_table_.size());
    spdlog::info("AnimationSync: real animation IDs need extraction from PAZ archives");
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
    if (source_model == target_model) return source_anim_id;

    uint64_t key = (static_cast<uint64_t>(source_model) << 32) | source_anim_id;
    auto it = anim_remap_table_.find(key);
    if (it != anim_remap_table_.end()) {
        return it->second;
    }

    // No mapping found - use the same ID (works for shared locomotion anims)
    return source_anim_id;
}

} // namespace cdcoop
