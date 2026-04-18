#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <cdcoop/core/game_structures.h>

namespace cdcoop {

// Packet types for co-op communication
enum class PacketType : uint8_t {
    // Session management
    HANDSHAKE           = 0x01,
    HANDSHAKE_ACK       = 0x02,
    DISCONNECT          = 0x03,
    HEARTBEAT           = 0x04,

    // Player state (high frequency - sent every tick)
    PLAYER_POSITION     = 0x10,
    PLAYER_ANIMATION    = 0x11,
    PLAYER_COMBAT       = 0x12,
    PLAYER_FULL_STATE   = 0x13,

    // World state (event-driven)
    ENEMY_STATE         = 0x20,
    ENEMY_DAMAGE        = 0x21,
    ENEMY_DEATH         = 0x22,
    ENEMY_SPAWN         = 0x23,

    // World interactions
    WORLD_INTERACT      = 0x30,
    QUEST_UPDATE        = 0x31,
    CUTSCENE_TRIGGER    = 0x32,
    LOOT_DROP           = 0x33,
    TELEPORT_TRIGGER    = 0x34,

    // System
    CONFIG_SYNC         = 0xF0,
    CHAT_MESSAGE        = 0xF1,
};

// Base packet header
#pragma pack(push, 1)
struct PacketHeader {
    uint8_t  magic[2] = {'C', 'D'};  // "CD" for Crimson Desert
    PacketType type;
    uint16_t payload_size;
    uint32_t sequence;
    uint32_t timestamp_ms;
};

struct PlayerPositionPacket {
    PacketHeader header;
    Vec3 position;
    Quat rotation;
    Vec3 velocity;
    uint8_t movement_flags; // grounded, jumping, sprinting, etc.
};

struct PlayerAnimationPacket {
    PacketHeader header;
    uint32_t animation_id;
    float blend_weight;
    float playback_speed;
    float normalized_time;
};

struct PlayerCombatPacket {
    PacketHeader header;
    uint8_t action;          // 0=attack, 1=dodge, 2=block, 3=skill
    uint32_t skill_id;
    Vec3 target_position;
    uint32_t target_entity_id;
};

struct PlayerFullStatePacket {
    PacketHeader header;
    Vec3 position;
    Quat rotation;
    Vec3 velocity;
    float health;
    float max_health;
    float stamina;
    uint32_t animation_id;
    float anim_blend;
    uint32_t weapon_id;
    uint8_t movement_flags;
};

struct EnemyStatePacket {
    PacketHeader header;
    uint32_t entity_id;
    Vec3 position;
    Quat rotation;
    float health;
    uint32_t state;
    uint32_t aggro_target_id; // 0=host, 1=client
};

struct EnemyDamagePacket {
    PacketHeader header;
    uint32_t entity_id;
    float damage;
    uint8_t damage_source; // 0=host, 1=client
};

struct WorldInteractPacket {
    PacketHeader header;
    uint32_t object_id;
    uint32_t interaction_type;
    uint32_t new_state;
};

// Fast-travel waypoint snapshot. Captured on the host at the
// waypoint-apply injection site (CrimsonDesert.exe+0xAB5594) and
// broadcast so the client knows the host is teleporting.
struct TeleportPacket {
    PacketHeader header;
    Vec3 destination;        // World-space waypoint (X, Y, Z)
    uint32_t waypoint_type;  // From [r15+0x00] — waypoint type id
};

struct HandshakePacket {
    PacketHeader header;
    uint32_t protocol_version;
    char player_name[64];
    uint32_t mod_version;
};
#pragma pack(pop)

// Packet serialization helpers
class PacketBuilder {
public:
    static std::vector<uint8_t> serialize(const PacketHeader& header, const void* payload, size_t size);

    template<typename T>
    static std::vector<uint8_t> serialize(const T& packet) {
        return std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&packet),
            reinterpret_cast<const uint8_t*>(&packet) + sizeof(T)
        );
    }

    static bool validate(const uint8_t* data, size_t size);
};

} // namespace cdcoop
