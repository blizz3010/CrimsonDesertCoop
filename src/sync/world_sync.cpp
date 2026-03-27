#include <cdcoop/sync/world_sync.h>
#include <cdcoop/network/session.h>
#include <cdcoop/core/config.h>
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

    spdlog::info("WorldSync initialized");
}

void WorldSync::shutdown() {}

void WorldSync::update(float delta_time) {
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
    spdlog::info("Quest update synced: quest {} -> stage {}", quest_id, stage);
}

void WorldSync::on_cutscene_trigger(uint32_t cutscene_id) {
    if (!get_config().sync_cutscenes) return;

    WorldInteractPacket pkt{};
    pkt.header.type = PacketType::CUTSCENE_TRIGGER;
    pkt.header.payload_size = sizeof(pkt) - sizeof(PacketHeader);
    pkt.object_id = cutscene_id;

    Session::instance().send_packet(pkt, true);
    spdlog::info("Cutscene triggered: {}", cutscene_id);
}

// --- Private ---

void WorldSync::on_remote_interact(const uint8_t* data, size_t size) {
    if (size < sizeof(WorldInteractPacket)) return;
    auto* pkt = reinterpret_cast<const WorldInteractPacket*>(data);

    spdlog::debug("Remote world interact: obj={}, type={}, state={}",
                   pkt->object_id, pkt->interaction_type, pkt->new_state);

    // TODO: find the world object and apply the state change
    // If we're host, validate the interaction
    // If we're client, apply the host's state change
}

void WorldSync::on_remote_quest_update(const uint8_t* data, size_t size) {
    if (size < sizeof(WorldInteractPacket)) return;
    auto* pkt = reinterpret_cast<const WorldInteractPacket*>(data);

    spdlog::info("Remote quest update: quest {} -> stage {}",
                  pkt->object_id, pkt->new_state);

    // TODO: update local quest state to match host
}

void WorldSync::on_remote_cutscene(const uint8_t* data, size_t size) {
    if (size < sizeof(WorldInteractPacket)) return;
    auto* pkt = reinterpret_cast<const WorldInteractPacket*>(data);

    spdlog::info("Remote cutscene trigger: {}", pkt->object_id);

    // TODO: trigger the cutscene locally
    // Both players should see the same cutscene simultaneously
}

} // namespace cdcoop
