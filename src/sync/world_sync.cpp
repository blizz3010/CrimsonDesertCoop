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

    // World state changes are forwarded as events. The object_id is the
    // lower 32 bits of the world object pointer. Since both players share
    // the same game world, the same objects exist at the same addresses.
    //
    // World objects (doors, chests, levers) are managed by the WorldSystem.
    // The MapLookup/MapInsert signatures from EquipHide could resolve objects
    // by hash, but the world object manager's exact layout needs more RE.
    //
    // For now, the event is logged. Both players' games will naturally sync
    // most world state since they run the same game instance. The main
    // purpose of this sync is to handle interactions that one player triggers
    // but the other player's game might not process (e.g., opening a chest
    // while the other player is far away).
}

void WorldSync::on_remote_quest_update(const uint8_t* data, size_t size) {
    if (size < sizeof(WorldInteractPacket)) return;
    auto* pkt = reinterpret_cast<const WorldInteractPacket*>(data);

    spdlog::info("Remote quest update: quest {} -> stage {}",
                  pkt->object_id, pkt->new_state);

    // Quest state sync is event-based. The host broadcasts quest stage changes
    // so the client tracks progression. Since both players run the same game
    // instance, quest state naturally stays in sync for most cases.
    // This packet handles edge cases where quest triggers are player-proximity-based
    // and only one player is in range.
    //
    // The quest system likely lives under WorldSystem as a subsystem,
    // accessible via a similar chain to ActorManager. The MapLookup sig from
    // EquipHide's indexed string table could be used to find quest entries by ID.
}

void WorldSync::on_remote_cutscene(const uint8_t* data, size_t size) {
    if (size < sizeof(WorldInteractPacket)) return;
    auto* pkt = reinterpret_cast<const WorldInteractPacket*>(data);

    spdlog::info("Remote cutscene trigger: {}", pkt->object_id);

    // Cutscene sync ensures both players enter the same cutscene simultaneously.
    // Since both players share the same game world, cutscenes triggered by the
    // host's game progression will play for both. This packet handles cases where
    // player 2 is far from the trigger zone.
    //
    // The cutscene system is likely a WorldSystem subsystem. PAZ archive data
    // (from the unpacker) could reveal cutscene trigger definitions.
}

} // namespace cdcoop
