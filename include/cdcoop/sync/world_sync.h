#pragma once

#include <cstdint>
#include <cdcoop/network/packet.h>

namespace cdcoop {

// Syncs world interactions (doors, chests, quest triggers, cutscenes)
// Host-authoritative: client interactions are forwarded to host for execution
class WorldSync {
public:
    static WorldSync& instance();

    void initialize();
    void shutdown();
    void update(float delta_time);

    // Local player interacted with a world object
    void on_world_interact(uint32_t object_id, uint32_t interaction_type, uint32_t new_state);

    // Quest progress changed
    void on_quest_update(uint32_t quest_id, uint32_t stage);

    // Cutscene triggered
    void on_cutscene_trigger(uint32_t cutscene_id);

private:
    WorldSync() = default;

    void on_remote_interact(const uint8_t* data, size_t size);
    void on_remote_quest_update(const uint8_t* data, size_t size);
    void on_remote_cutscene(const uint8_t* data, size_t size);
};

} // namespace cdcoop
