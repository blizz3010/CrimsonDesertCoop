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
    spdlog::warn("Quest update sent but receive-side is stub-only: quest {} -> stage {}", quest_id, stage);
}

void WorldSync::on_cutscene_trigger(uint32_t cutscene_id) {
    if (!get_config().sync_cutscenes) return;

    WorldInteractPacket pkt{};
    pkt.header.type = PacketType::CUTSCENE_TRIGGER;
    pkt.header.payload_size = sizeof(pkt) - sizeof(PacketHeader);
    pkt.object_id = cutscene_id;

    Session::instance().send_packet(pkt, true);
    spdlog::warn("Cutscene trigger sent but receive-side is stub-only: {}", cutscene_id);
}

// --- Private ---

void WorldSync::on_remote_interact(const uint8_t* data, size_t size) {
    if (size < sizeof(WorldInteractPacket)) return;
    auto* pkt = reinterpret_cast<const WorldInteractPacket*>(data);

    spdlog::warn("World interact received but not applied (stub): obj={}, type={}, state={}",
                  pkt->object_id, pkt->interaction_type, pkt->new_state);

    // STUB: World object manager layout is unknown. MapLookup/MapInsert sigs
    // from EquipHide exist but the manager struct needs more RE work.
    // Both players share the same game world, so most interactions sync
    // naturally. This handler would cover edge cases where one player
    // triggers something while the other is out of range.
}

void WorldSync::on_remote_quest_update(const uint8_t* data, size_t size) {
    if (size < sizeof(WorldInteractPacket)) return;
    auto* pkt = reinterpret_cast<const WorldInteractPacket*>(data);

    spdlog::warn("Quest update received but not applied (stub): quest {} -> stage {}",
                  pkt->object_id, pkt->new_state);

    // STUB: Quest manager pointer not yet found. The quest system likely lives
    // under WorldSystem as a subsystem. Quest state mostly syncs naturally
    // since both players share the same game world.
}

void WorldSync::on_remote_cutscene(const uint8_t* data, size_t size) {
    if (size < sizeof(WorldInteractPacket)) return;
    auto* pkt = reinterpret_cast<const WorldInteractPacket*>(data);

    spdlog::warn("Cutscene trigger received but not applied (stub): {}",
                  pkt->object_id);

    // STUB: Cutscene manager not yet found. Cutscenes triggered by the host's
    // progression play for both players since they share the same game world.
    // This handler would cover cases where Player 2 is far from the trigger zone.
}

} // namespace cdcoop
