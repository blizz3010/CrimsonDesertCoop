#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <cdcoop/network/packet.h>

namespace cdcoop {

enum class SessionState {
    DISCONNECTED,
    HOSTING,        // Waiting for client to join
    CONNECTING,     // Client attempting to connect
    CONNECTED,      // Active co-op session
};

enum class SessionRole {
    NONE,
    HOST,           // Runs the game world, authoritative
    CLIENT,         // Receives world state from host
};

// Callback for incoming packets
using PacketCallback = std::function<void(PacketType type, const uint8_t* data, size_t size)>;

// Abstract network transport interface
class INetworkTransport {
public:
    virtual ~INetworkTransport() = default;

    virtual bool host(uint16_t port) = 0;
    virtual bool connect(const std::string& address, uint16_t port) = 0;
    virtual void disconnect() = 0;

    virtual bool send(const uint8_t* data, size_t size, bool reliable = true) = 0;
    virtual void poll() = 0; // Process incoming messages

    virtual bool is_connected() const = 0;
    virtual std::string peer_name() const = 0;

    void set_packet_callback(PacketCallback cb) { on_packet_ = std::move(cb); }

protected:
    PacketCallback on_packet_;
};

// Co-op session manager
class Session {
public:
    static Session& instance();

    // Host a game - other player joins via Steam friend invite or IP
    bool host_session();

    // Join a session by Steam friend or IP
    bool join_session(const std::string& target);

    // Leave the current session
    void leave_session();

    // Send a packet to the other player
    void send(const uint8_t* data, size_t size, bool reliable = true);

    template<typename T>
    void send_packet(const T& packet, bool reliable = true) {
        send(reinterpret_cast<const uint8_t*>(&packet), sizeof(T), reliable);
    }

    // Must be called every game tick to process network messages
    void update(float delta_time);

    SessionState state() const { return state_; }
    SessionRole role() const { return role_; }
    bool is_active() const { return state_ == SessionState::CONNECTED; }
    bool is_host() const { return role_ == SessionRole::HOST; }

    // Register handlers for specific packet types
    void register_handler(PacketType type, PacketCallback handler);

    // Peer info
    std::string peer_name() const;
    float ping_ms() const { return ping_ms_; }

private:
    Session() = default;

    void on_packet_received(PacketType type, const uint8_t* data, size_t size);
    void handle_handshake(const uint8_t* data, size_t size);
    void send_heartbeat();

    std::unique_ptr<INetworkTransport> transport_;
    SessionState state_ = SessionState::DISCONNECTED;
    SessionRole role_ = SessionRole::NONE;

    std::unordered_map<PacketType, PacketCallback> handlers_;
    std::mutex handler_mutex_;

    uint32_t sequence_ = 0;
    float heartbeat_timer_ = 0.0f;
    float ping_ms_ = 0.0f;
    float time_since_last_recv_ = 0.0f;
    static constexpr float HEARTBEAT_INTERVAL = 1.0f;
    static constexpr float TIMEOUT_SECONDS = 10.0f;
};

} // namespace cdcoop
