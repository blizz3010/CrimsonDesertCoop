#include <cdcoop/sync/mount_sync.h>
#include <cdcoop/network/session.h>
#include <cdcoop/core/config.h>
#include <cdcoop/core/game_structures.h>
#include <cdcoop/core/hooks.h>
#include <spdlog/spdlog.h>
#include <Windows.h>

namespace cdcoop {

MountSync& MountSync::instance() {
    static MountSync inst;
    return inst;
}

void MountSync::initialize() {
    auto& session = Session::instance();

    session.register_handler(PacketType::MOUNT_STATE,
        [this](PacketType, const uint8_t* data, size_t size) {
            on_remote_mount_state(data, size);
        });

    spdlog::info("MountSync initialized");
}

void MountSync::shutdown() {
    local_  = {};
    remote_ = {};
    last_broadcast_mounted_ = false;
}

void MountSync::update(float delta_time) {
    // Only broadcast while session is active and the feature is enabled.
    // Reading the mount is harmless without a session, but spamming
    // broadcasts would be wasteful.
    auto& session = Session::instance();
    auto& cfg = get_config();

    MountView view{};
    bool still_mounted = poll_local_mount(view);
    local_ = view;

    if (!cfg.sync_mount_state || !session.is_active()) return;

    send_timer_ += delta_time;

    // Always notify immediately on mount/dismount edge so the remote
    // overlay clears or lights up without waiting for the 5Hz tick.
    const bool edge = still_mounted != last_broadcast_mounted_;
    if (!edge && send_timer_ < MOUNT_SYNC_RATE) return;

    send_timer_ = 0.0f;
    last_broadcast_mounted_ = still_mounted;
    broadcast(local_);
}

bool MountSync::poll_local_mount(MountView& out) {
    auto& rt = get_runtime_offsets();
    if (!rt.mount_resolved || !is_valid_ptr(rt.mount_ptr)) {
        out.is_mounted = false;
        return false;
    }

    // Validate the pointer before dereferencing. Mount entity lifetime
    // isn't guaranteed — the game may deallocate the struct on dismount
    // and the capture hook doesn't re-fire on state change.
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<void*>(rt.mount_ptr), &mbi, sizeof(mbi)) == 0 ||
        !(mbi.State & MEM_COMMIT)) {
        rt.mount_resolved = false;
        rt.mount_ptr = 0;
        out.is_mounted = false;
        return false;
    }

    uintptr_t stat_base = resolve_ptr_chain(rt.mount_ptr, {offsets::Player::STAT_COMPONENT});
    if (!is_valid_ptr(stat_base)) {
        out.is_mounted = false;
        return false;
    }

    // HP is the first stat entry (same layout as player):
    // stat_component + 0x08 = current_value (int64, value*1000)
    // stat_component + 0x18 = max_value
    int64_t cur_hp = read_mem<int64_t>(stat_base, StatEntry::CURRENT_VALUE);
    int64_t max_hp = read_mem<int64_t>(stat_base, StatEntry::MAX_VALUE);

    // Stamina lives at the player-side offset (+0x488 from stat_base)
    // for both player and mount in the Orcax scanner's layout.
    int64_t cur_st = read_mem<int64_t>(stat_base, StatEntry::STAMINA_FROM_HEALTH + StatEntry::CURRENT_VALUE);
    int64_t max_st = read_mem<int64_t>(stat_base, StatEntry::STAMINA_FROM_HEALTH + StatEntry::MAX_VALUE);

    // Sanity: if HP is zero or nonsense, treat as dismounted.
    if (max_hp <= 0 || max_hp > offsets::Mount::HP_SANITY_MAX_RAW) {
        out.is_mounted = false;
        return false;
    }

    out.is_mounted  = true;
    out.health      = static_cast<float>(cur_hp) / 1000.0f;
    out.max_health  = static_cast<float>(max_hp) / 1000.0f;
    out.stamina     = (max_st > 0) ? static_cast<float>(cur_st) / 1000.0f : 0.0f;
    out.max_stamina = (max_st > 0) ? static_cast<float>(max_st) / 1000.0f : 0.0f;
    out.mount_entity_id = static_cast<uint32_t>(rt.mount_ptr & 0xFFFFFFFF);

    // Mount type hash = low 32 bits of vtable pointer. Stable for the
    // same mount class across sessions, good enough for "is the remote
    // on the same mount type as me?" checks in the overlay.
    uintptr_t vtable = read_mem<uintptr_t>(rt.mount_ptr, 0);
    out.mount_type_hash = static_cast<uint32_t>(vtable & 0xFFFFFFFF);
    return true;
}

void MountSync::broadcast(const MountView& view) {
    MountStatePacket pkt{};
    pkt.header.type = PacketType::MOUNT_STATE;
    pkt.header.payload_size = sizeof(pkt) - sizeof(PacketHeader);
    pkt.mount_entity_id = view.mount_entity_id;
    pkt.mount_type_hash = view.mount_type_hash;
    pkt.health          = view.health;
    pkt.max_health      = view.max_health;
    pkt.stamina         = view.stamina;
    pkt.max_stamina     = view.max_stamina;
    pkt.is_mounted      = view.is_mounted ? 1 : 0;

    // Unreliable: mount HP tracks the player's combat state and is fine
    // to drop. The edge-triggered broadcast above uses the same channel;
    // if a mount/dismount edge packet is dropped the next 5Hz tick will
    // resolve it in ≤200ms.
    Session::instance().send_packet(pkt, false);
}

void MountSync::on_remote_mount_state(const uint8_t* data, size_t size) {
    if (size < sizeof(MountStatePacket)) return;
    auto* pkt = reinterpret_cast<const MountStatePacket*>(data);

    remote_.is_mounted      = (pkt->is_mounted != 0);
    remote_.health          = pkt->health;
    remote_.max_health      = pkt->max_health;
    remote_.stamina         = pkt->stamina;
    remote_.max_stamina     = pkt->max_stamina;
    remote_.mount_entity_id = pkt->mount_entity_id;
    remote_.mount_type_hash = pkt->mount_type_hash;
}

} // namespace cdcoop
