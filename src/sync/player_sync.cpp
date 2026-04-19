#include <cdcoop/sync/player_sync.h>
#include <cdcoop/network/session.h>
#include <cdcoop/player/companion_hijack.h>
#include <cdcoop/core/config.h>
#include <cdcoop/player/player_manager.h>
#include <spdlog/spdlog.h>
#include <cmath>
#include <cstring>

namespace cdcoop {

namespace {
// A Vec3/Quat from a remote peer can arrive NaN/inf if the packet was
// corrupted, or if a peer with mismatched code sent garbage. Once a
// non-finite value lands in interpolated_state_, the lerp in
// interpolate_remote_state poisons every subsequent update and the
// NaN eventually gets written into the companion entity's position
// struct in game memory — which the engine handles very badly.
bool is_finite_vec3(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool is_finite_quat(const Quat& q) {
    return std::isfinite(q.x) && std::isfinite(q.y) &&
           std::isfinite(q.z) && std::isfinite(q.w);
}
} // namespace

PlayerSync& PlayerSync::instance() {
    static PlayerSync inst;
    return inst;
}

void PlayerSync::initialize() {
    auto& session = Session::instance();

    session.register_handler(PacketType::PLAYER_POSITION,
        [this](PacketType, const uint8_t* data, size_t size) {
            on_remote_position(data, size);
        });

    session.register_handler(PacketType::PLAYER_ANIMATION,
        [this](PacketType, const uint8_t* data, size_t size) {
            on_remote_animation(data, size);
        });

    session.register_handler(PacketType::PLAYER_COMBAT,
        [this](PacketType, const uint8_t* data, size_t size) {
            on_remote_combat(data, size);
        });

    session.register_handler(PacketType::PLAYER_FULL_STATE,
        [this](PacketType, const uint8_t* data, size_t size) {
            on_remote_full_state(data, size);
        });

    auto& cfg = get_config();
    tether_distance_sq_ = cfg.tether_distance * cfg.tether_distance;

    spdlog::info("PlayerSync initialized (tether dist: {}m)", cfg.tether_distance);
}

void PlayerSync::shutdown() {
    // Nothing to clean up
}

void PlayerSync::update(float delta_time) {
    auto& session = Session::instance();
    if (!session.is_active()) return;

    auto& pm = PlayerManager::instance();
    // Don't broadcast garbage while we're waiting for the player pointer
    // to resolve (mod can init on the main menu now — see PlayerManager::
    // initialize). Without this guard the peer would see us teleporting
    // to {0,0,0} every frame until the world finishes loading.
    if (pm.local_player() == 0) return;

    // Send our position at fixed rate
    send_timer_ += delta_time;
    if (send_timer_ >= POSITION_SEND_RATE) {
        send_timer_ = 0.0f;

        Vec3 pos = pm.local_position();
        Quat rot = pm.local_rotation();
        on_local_position_changed(pos, rot, {0, 0, 0}, 0);
    }

    // Send full state periodically as a fallback sync mechanism
    full_state_timer_ += delta_time;
    if (full_state_timer_ >= FULL_STATE_RATE) {
        full_state_timer_ = 0.0f;

        auto& rt = get_runtime_offsets();
        PlayerFullStatePacket pkt{};
        pkt.header.type = PacketType::PLAYER_FULL_STATE;
        pkt.header.payload_size = sizeof(pkt) - sizeof(PacketHeader);
        pkt.position = pm.local_position();
        pkt.rotation = pm.local_rotation();
        pkt.health = pm.local_health();
        pkt.max_health = pm.local_max_health();
        pkt.movement_flags = 0;

        // Read current animation state from player actor memory
        if (is_valid_ptr(rt.player_actor_ptr)) {
            pkt.animation_id = read_mem<uint32_t>(rt.player_actor_ptr, offsets::Player::ANIM_STATE);
            pkt.anim_blend = read_mem<float>(rt.player_actor_ptr, offsets::Player::ANIM_BLEND);
        }

        session.send_packet(pkt);
    }

    // Interpolate remote player for smooth rendering
    interpolate_remote_state(delta_time);

    // Apply interpolated state to the companion entity
    auto& hijack = CompanionHijack::instance();
    if (hijack.is_active()) {
        hijack.set_position(interpolated_state_.position, interpolated_state_.rotation);
        hijack.set_animation(interpolated_state_.animation_id, interpolated_state_.anim_blend,
                             1.0f, 0.0f);
        hijack.set_health(interpolated_state_.health, interpolated_state_.max_health);
    }

    // Tether check
    Vec3 local_pos = pm.local_position();
    Vec3 diff = interpolated_state_.position - local_pos;
    float dist_sq = diff.length_sq();
    tether_active_ = (dist_sq > tether_distance_sq_);
}

void PlayerSync::on_local_position_changed(const Vec3& pos, const Quat& rot,
                                            const Vec3& vel, uint8_t flags) {
    PlayerPositionPacket pkt{};
    pkt.header.type = PacketType::PLAYER_POSITION;
    pkt.header.payload_size = sizeof(pkt) - sizeof(PacketHeader);
    pkt.position = pos;
    pkt.rotation = rot;
    pkt.velocity = vel;
    pkt.movement_flags = flags;

    // Position updates are unreliable (high frequency, stale data is useless)
    Session::instance().send_packet(pkt, false);
}

void PlayerSync::on_local_animation_changed(uint32_t anim_id, float blend,
                                             float speed, float time) {
    PlayerAnimationPacket pkt{};
    pkt.header.type = PacketType::PLAYER_ANIMATION;
    pkt.header.payload_size = sizeof(pkt) - sizeof(PacketHeader);
    pkt.animation_id = anim_id;
    pkt.blend_weight = blend;
    pkt.playback_speed = speed;
    pkt.normalized_time = time;

    // Animation changes are reliable (we need them all)
    Session::instance().send_packet(pkt, true);
}

void PlayerSync::on_local_combat_action(uint8_t action, uint32_t skill_id,
                                         const Vec3& target_pos, uint32_t target_id) {
    PlayerCombatPacket pkt{};
    pkt.header.type = PacketType::PLAYER_COMBAT;
    pkt.header.payload_size = sizeof(pkt) - sizeof(PacketHeader);
    pkt.action = action;
    pkt.skill_id = skill_id;
    pkt.target_position = target_pos;
    pkt.target_entity_id = target_id;

    Session::instance().send_packet(pkt, true);
}

Vec3 PlayerSync::get_tether_pull_direction() const {
    auto local_pos = PlayerManager::instance().local_position();
    Vec3 diff = interpolated_state_.position - local_pos;
    float len_sq = diff.length_sq();
    if (len_sq < 0.001f) return {0, 0, 0};
    float inv_len = 1.0f / std::sqrt(len_sq);
    return diff * inv_len;
}

// --- Private ---

void PlayerSync::on_remote_position(const uint8_t* data, size_t size) {
    if (size < sizeof(PlayerPositionPacket)) return;
    auto* pkt = reinterpret_cast<const PlayerPositionPacket*>(data);

    // Drop anything non-finite before it reaches the interpolator.
    if (!is_finite_vec3(pkt->position) || !is_finite_quat(pkt->rotation) ||
        !is_finite_vec3(pkt->velocity)) {
        spdlog::warn("Dropping non-finite remote position packet");
        return;
    }

    // Add to interpolation buffer
    auto& slot = state_buffer_[state_buffer_head_];
    slot.position = pkt->position;
    slot.rotation = pkt->rotation;
    slot.velocity = pkt->velocity;
    slot.movement_flags = pkt->movement_flags;

    state_buffer_head_ = (state_buffer_head_ + 1) % STATE_BUFFER_SIZE;
    if (state_buffer_count_ < STATE_BUFFER_SIZE) state_buffer_count_++;
}

void PlayerSync::on_remote_animation(const uint8_t* data, size_t size) {
    if (size < sizeof(PlayerAnimationPacket)) return;
    auto* pkt = reinterpret_cast<const PlayerAnimationPacket*>(data);

    interpolated_state_.animation_id = pkt->animation_id;
    interpolated_state_.anim_blend = pkt->blend_weight;
}

void PlayerSync::on_remote_combat(const uint8_t* data, size_t size) {
    if (size < sizeof(PlayerCombatPacket)) return;
    auto* pkt = reinterpret_cast<const PlayerCombatPacket*>(data);

    spdlog::debug("Remote combat action: type={}, skill={}", pkt->action, pkt->skill_id);

    // Mirror the remote player's last known animation on the companion entity.
    // This avoids needing real combat animation IDs - we just replay whatever
    // anim the remote player was performing when the combat packet was sent.
    auto& hijack = CompanionHijack::instance();
    if (hijack.is_active()) {
        hijack.set_animation(interpolated_state_.animation_id, interpolated_state_.anim_blend,
                             1.0f, 0.0f);
    }

    // Damage is handled by the damage_calc_detour hook when the companion entity
    // attacks an enemy. No need to send a redundant 0-damage report here.
}

void PlayerSync::on_remote_full_state(const uint8_t* data, size_t size) {
    if (size < sizeof(PlayerFullStatePacket)) return;
    auto* pkt = reinterpret_cast<const PlayerFullStatePacket*>(data);

    // Same finite-check as on_remote_position; full-state packets write
    // into the same interpolation buffer so the same NaN-poisoning
    // hazard exists here.
    if (!is_finite_vec3(pkt->position) || !is_finite_quat(pkt->rotation) ||
        !is_finite_vec3(pkt->velocity) ||
        !std::isfinite(pkt->health) || !std::isfinite(pkt->max_health)) {
        spdlog::warn("Dropping non-finite remote full-state packet");
        return;
    }

    // Full state overwrites interpolation buffer
    auto& slot = state_buffer_[state_buffer_head_];
    memcpy(&slot, pkt, sizeof(PlayerFullStatePacket));
    state_buffer_head_ = (state_buffer_head_ + 1) % STATE_BUFFER_SIZE;
    if (state_buffer_count_ < STATE_BUFFER_SIZE) state_buffer_count_++;
}

void PlayerSync::interpolate_remote_state(float delta_time) {
    if (state_buffer_count_ == 0) return;

    // Simple lerp interpolation toward the latest received state
    int latest = (state_buffer_head_ - 1 + STATE_BUFFER_SIZE) % STATE_BUFFER_SIZE;
    auto& target = state_buffer_[latest];

    float t = std::min(1.0f, delta_time * 15.0f); // Smooth factor

    interpolated_state_.position.x += (target.position.x - interpolated_state_.position.x) * t;
    interpolated_state_.position.y += (target.position.y - interpolated_state_.position.y) * t;
    interpolated_state_.position.z += (target.position.z - interpolated_state_.position.z) * t;

    // Normalized lerp (nlerp) for quaternion interpolation.
    // Handle antipodal case: if dot product < 0, negate target to take shortest path.
    auto& cur = interpolated_state_.rotation;
    Quat tgt = target.rotation;
    float dot = cur.x * tgt.x + cur.y * tgt.y + cur.z * tgt.z + cur.w * tgt.w;
    if (dot < 0.0f) {
        tgt.x = -tgt.x; tgt.y = -tgt.y; tgt.z = -tgt.z; tgt.w = -tgt.w;
    }
    cur.x += (tgt.x - cur.x) * t;
    cur.y += (tgt.y - cur.y) * t;
    cur.z += (tgt.z - cur.z) * t;
    cur.w += (tgt.w - cur.w) * t;
    // Re-normalize to maintain unit quaternion
    float len = std::sqrt(cur.x * cur.x + cur.y * cur.y + cur.z * cur.z + cur.w * cur.w);
    if (len > 0.0001f) {
        float inv = 1.0f / len;
        cur.x *= inv; cur.y *= inv; cur.z *= inv; cur.w *= inv;
    }

    interpolated_state_.health = target.health;
    interpolated_state_.max_health = target.max_health;
}

} // namespace cdcoop
