#include <cdcoop/sync/animation_sync.h>
#include <cdcoop/core/game_structures.h>
#include <spdlog/spdlog.h>

#include <atomic>

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
    spdlog::warn("AnimationSync: player_animation_hook is NOT installed - animation updates are "
                 "only sent via 5Hz full state packets. Remote player animations may appear choppy.");
}

void AnimationSync::shutdown() {
    anim_remap_table_.clear();
}

void AnimationSync::apply_remote_animation(uintptr_t entity_ptr, uint32_t anim_id,
                                            float blend_weight, float /*speed*/,
                                            float /*normalized_time*/) {
    if (!is_valid_ptr(entity_ptr)) return;

    auto& rt = get_runtime_offsets();

    // Preferred path: write through the AnimationEvaluator captured by the
    // experimental hook (CDAnimCancel research). The evaluator is the real
    // owner of animation playback; actor+0x120/0x124 are unverified estimates.
    if (rt.animation_evaluator_resolved && is_valid_ptr(rt.animation_evaluator_ptr)) {
        write_mem<uint32_t>(rt.animation_evaluator_ptr,
                            offsets::AnimationEvaluator::STATE, anim_id);
        write_mem<float>(rt.animation_evaluator_ptr,
                         offsets::AnimationEvaluator::BLEND_WEIGHT, blend_weight);

        static std::atomic<bool> logged_evaluator_mode{false};
        bool expected = false;
        if (logged_evaluator_mode.compare_exchange_strong(expected, true)) {
            spdlog::info("AnimationSync: evaluator mode active (writing to evaluator 0x{:X})",
                         rt.animation_evaluator_ptr);
        }
        return;
    }

    // Fallback: write to the deprecated actor+0x120/0x124 fields. These are
    // very likely inert in-game (per CDAnimCancel), but we keep the write so
    // that if a future build exposes anything useful at those offsets, or if
    // someone patches their copy, it still has a chance to land.
    write_mem<uint32_t>(entity_ptr, offsets::Companion::ANIM_STATE, anim_id);
    write_mem<float>(entity_ptr, offsets::Player::ANIM_BLEND, blend_weight);
}

uint32_t AnimationSync::remap_animation(uint32_t source_anim_id, int /*source_model*/,
                                         int /*target_model*/) {
    // Passthrough mode: always use the source anim ID directly.
    // Cross-model remapping requires real animation IDs extracted from PAZ archives.
    // For now, this works correctly when source_model == target_model.
    return source_anim_id;
}

} // namespace cdcoop
