#include <cdcoop/sync/world_sync.h>
#include <cdcoop/network/session.h>
#include <cdcoop/core/config.h>
#include <cdcoop/core/game_structures.h>
#include <spdlog/spdlog.h>

namespace cdcoop {

WorldSync& WorldSync::instance() {
    static WorldSync inst;
    return inst;
}

void WorldSync::initialize() {
    auto& session = Session::instance();

    session.register_handler(PacketType::WORLD_INTERACT,
        [this](PacketType, const uint8_t* data, size_t size) {
            on_remote_interact(data, size);
        });

    session.register_handler(PacketType::QUEST_UPDATE,
        [this](PacketType, const uint8_t* data, size_t size) {
            on_remote_quest_update(data, size);
        });

    session.register_handler(PacketType::CUTSCENE_TRIGGER,
        [this](PacketType, const uint8_t* data, size_t size) {
            on_remote_cutscene(data, size);
        });

    session.register_handler(PacketType::TELEPORT_TRIGGER,
        [this](PacketType, const uint8_t* data, size_t size) {
            on_remote_teleport(data, size);
        });

    spdlog::info("WorldSync initialized");
}

void WorldSync::shutdown() {}

void WorldSync::update([[maybe_unused]] float delta_time) {
    // World sync is event-driven, no periodic updates needed
}

void WorldSync::on_world_interact(uint32_t object_id, uint32_t interaction_type,
                                   uint32_t new_state) {
    WorldInteractPacket pkt{};
    pkt.header.type = PacketType::WORLD_INTERACT;
    pkt.header.payload_size = sizeof(pkt) - sizeof(PacketHeader);
    pkt.object_id = object_id;
    pkt.interaction_type = interaction_type;
    pkt.new_state = new_state;

    Session::instance().send_packet(pkt, true);
}

void WorldSync::on_quest_update(uint32_t quest_id, uint32_t stage) {
    if (!get_config().sync_quest_progress) return;

    // Only host broadcasts quest progress
    if (!Session::instance().is_host()) return;

    WorldInteractPacket pkt{};
    pkt.header.type = PacketType::QUEST_UPDATE;
    pkt.header.payload_size = sizeof(pkt) - sizeof(PacketHeader);
    pkt.object_id = quest_id;
    pkt.new_state = stage;

    Session::instance().send_packet(pkt, true);
    spdlog::debug("Quest update sent (receive-side stub): quest {} -> stage {}", quest_id, stage);
}

void WorldSync::on_cutscene_trigger(uint32_t cutscene_id) {
    if (!get_config().sync_cutscenes) return;

    WorldInteractPacket pkt{};
    pkt.header.type = PacketType::CUTSCENE_TRIGGER;
    pkt.header.payload_size = sizeof(pkt) - sizeof(PacketHeader);
    pkt.object_id = cutscene_id;

    Session::instance().send_packet(pkt, true);
    spdlog::debug("Cutscene trigger sent (receive-side stub): {}", cutscene_id);
}

void WorldSync::on_teleport_trigger(const Vec3& destination, uint32_t waypoint_type) {
    // The teleport hook detour gates send-side on host already, but keep the
    // belt-and-suspenders check here in case anything else calls this method.
    if (!Session::instance().is_host()) return;

    TeleportPacket pkt{};
    pkt.header.type = PacketType::TELEPORT_TRIGGER;
    pkt.header.payload_size = sizeof(pkt) - sizeof(PacketHeader);
    pkt.destination = destination;
    pkt.waypoint_type = waypoint_type;

    Session::instance().send_packet(pkt, true);
    spdlog::debug("Teleport trigger sent: type=0x{:X} dest=({},{},{})",
                  waypoint_type, destination.x, destination.y, destination.z);
}

// --- Private ---

void WorldSync::on_remote_interact(const uint8_t* data, size_t size) {
    if (size < sizeof(WorldInteractPacket)) return;
    auto* pkt = reinterpret_cast<const WorldInteractPacket*>(data);

    // The world-object manager's exact layout is unknown. When the
    // world-system probe has run it caches a candidate pointer we *might*
    // be able to use; log that here so telemetry bridges the gap. Writes
    // are still disabled until a real dispatch function is identified.
    auto& rt = get_runtime_offsets();
    spdlog::debug("World interact received (stub): obj={}, type={}, state={}, candidate_mgr=0x{:X}",
                  pkt->object_id, pkt->interaction_type, pkt->new_state,
                  rt.world_object_manager_candidate);
}

void WorldSync::on_remote_quest_update(const uint8_t* data, size_t size) {
    if (size < sizeof(WorldInteractPacket)) return;
    auto* pkt = reinterpret_cast<const WorldInteractPacket*>(data);

    auto& rt = get_runtime_offsets();
    spdlog::debug("Quest update received (stub): quest {} -> stage {}, candidate_mgr=0x{:X}",
                  pkt->object_id, pkt->new_state, rt.quest_manager_candidate);
}

void WorldSync::on_remote_cutscene(const uint8_t* data, size_t size) {
    if (size < sizeof(WorldInteractPacket)) return;
    auto* pkt = reinterpret_cast<const WorldInteractPacket*>(data);

    auto& rt = get_runtime_offsets();
    spdlog::debug("Cutscene trigger received (stub): {}, candidate_mgr=0x{:X}",
                  pkt->object_id, rt.cutscene_manager_candidate);
}

void WorldSync::on_remote_teleport(const uint8_t* data, size_t size) {
    if (size < sizeof(TeleportPacket)) return;
    auto* pkt = reinterpret_cast<const TeleportPacket*>(data);

    // Apply path is intentionally log-only for now. The proper apply
    // function (the one that consumes [r14+0xD8] / [r14+0xE0] and produces
    // a real area transition) is unidentified. The 30Hz position broadcast
    // already pulls the client-controlled companion entity along once the
    // host arrives, so this notification is informational while we look
    // for the apply function. See docs/RESEARCH_2026-04-18.md #7.
    spdlog::info("Host fast-travel announced: type=0x{:X} dest=({},{},{}) — apply pending",
                 pkt->waypoint_type, pkt->destination.x, pkt->destination.y, pkt->destination.z);
}

} // namespace cdcoop
