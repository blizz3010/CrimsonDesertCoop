#include <cdcoop/network/session.h>
#include <cdcoop/network/steam_network.h>
#include <cdcoop/core/config.h>
#include <cdcoop/sync/player_sync.h>
#include <cdcoop/sync/enemy_sync.h>
#include <cdcoop/player/companion_hijack.h>
#include <cdcoop/player/player_manager.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <vector>

namespace cdcoop {

Session& Session::instance() {
    static Session inst;
    return inst;
}

bool Session::host_session() {
    if (state_ != SessionState::DISCONNECTED) {
        spdlog::warn("Cannot host: already in state {}", static_cast<int>(state_));
        return false;
    }

    auto& cfg = get_config();

#if CDCOOP_STEAM
    if (cfg.use_steam_networking) {
        transport_ = std::make_unique<SteamNetworkTransport>();
    }
#endif

    if (!transport_) {
        spdlog::error("No network transport available");
        return false;
    }

    transport_->set_packet_callback([this](PacketType type, const uint8_t* data, size_t size) {
        on_packet_received(type, data, size);
    });

    if (!transport_->host(cfg.port)) {
        spdlog::error("Failed to start hosting on port {}", cfg.port);
        return false;
    }

    role_ = SessionRole::HOST;
    state_ = SessionState::HOSTING;
    spdlog::info("Hosting co-op session on port {}...", cfg.port);
    spdlog::info("Waiting for player 2 to connect");

    return true;
}

bool Session::join_session(const std::string& target) {
    if (state_ != SessionState::DISCONNECTED) {
        spdlog::warn("Cannot join: already in state {}", static_cast<int>(state_));
        return false;
    }

    auto& cfg = get_config();

#if CDCOOP_STEAM
    if (cfg.use_steam_networking) {
        transport_ = std::make_unique<SteamNetworkTransport>();
    }
#endif

    if (!transport_) {
        spdlog::error("No network transport available");
        return false;
    }

    transport_->set_packet_callback([this](PacketType type, const uint8_t* data, size_t size) {
        on_packet_received(type, data, size);
    });

    if (!transport_->connect(target, cfg.port)) {
        spdlog::error("Failed to connect to {}", target);
        return false;
    }

    role_ = SessionRole::CLIENT;
    state_ = SessionState::CONNECTING;
    spdlog::info("Connecting to {}...", target);

    // Send handshake
    HandshakePacket hs{};
    hs.header.type = PacketType::HANDSHAKE;
    hs.header.payload_size = sizeof(HandshakePacket) - sizeof(PacketHeader);
    hs.protocol_version = 1;
    strncpy(hs.player_name, cfg.player_name.c_str(), sizeof(hs.player_name) - 1);
    hs.mod_version = 1;
    send_packet(hs);

    return true;
}

void Session::leave_session() {
    if (state_ == SessionState::DISCONNECTED) return;

    spdlog::info("Leaving session...");

    // Clean up co-op state
    CompanionHijack::instance().deactivate();
    PlayerManager::instance().despawn_remote_player();
    EnemySync::instance().revert_coop_scaling();

    if (transport_) {
        // Send disconnect packet
        PacketHeader dc{};
        dc.type = PacketType::DISCONNECT;
        dc.payload_size = 0;
        send(reinterpret_cast<const uint8_t*>(&dc), sizeof(dc));
        transport_->disconnect();
        transport_.reset();
    }

    state_ = SessionState::DISCONNECTED;
    role_ = SessionRole::NONE;
    sequence_ = 0;
    spdlog::info("Session ended");
}

void Session::send(const uint8_t* data, size_t size, bool reliable) {
    if (transport_ && transport_->is_connected()) {
        if (size >= sizeof(PacketHeader)) {
            // Copy packet data to stamp sequence/timestamp without mutating caller's data
            std::vector<uint8_t> buf(data, data + size);
            auto* hdr = reinterpret_cast<PacketHeader*>(buf.data());
            hdr->sequence = sequence_++;
            hdr->timestamp_ms = static_cast<uint32_t>(GetTickCount64() & 0xFFFFFFFF);
            transport_->send(buf.data(), buf.size(), reliable);
        } else {
            transport_->send(data, size, reliable);
        }
    }
}

void Session::update(float delta_time) {
    if (!transport_) return;

    // Poll for incoming messages
    transport_->poll();

    // Heartbeat
    heartbeat_timer_ += delta_time;
    if (heartbeat_timer_ >= HEARTBEAT_INTERVAL) {
        send_heartbeat();
        heartbeat_timer_ = 0.0f;
    }

    // Timeout detection
    if (state_ == SessionState::CONNECTED) {
        time_since_last_recv_ += delta_time;
        if (time_since_last_recv_ >= TIMEOUT_SECONDS) {
            spdlog::warn("Connection timed out");
            leave_session();
        }
    }
}

void Session::register_handler(PacketType type, PacketCallback handler) {
    std::lock_guard lock(handler_mutex_);
    handlers_[type] = std::move(handler);
}

std::string Session::peer_name() const {
    return transport_ ? transport_->peer_name() : "";
}

void Session::on_packet_received(PacketType type, const uint8_t* data, size_t size) {
    time_since_last_recv_ = 0.0f;

    // Handle system packets internally
    switch (type) {
        case PacketType::HANDSHAKE:
        case PacketType::HANDSHAKE_ACK:
            handle_handshake(data, size);
            return;
        case PacketType::DISCONNECT:
            spdlog::info("Peer disconnected");
            leave_session();
            return;
        case PacketType::HEARTBEAT:
            return; // Just resets timeout timer (done above)
        default:
            break;
    }

    // Forward to registered handlers
    std::lock_guard lock(handler_mutex_);
    auto it = handlers_.find(type);
    if (it != handlers_.end()) {
        it->second(type, data, size);
    }
}

void Session::handle_handshake(const uint8_t* data, size_t size) {
    if (size < sizeof(HandshakePacket)) return;

    auto* hs = reinterpret_cast<const HandshakePacket*>(data);

    if (hs->header.type == PacketType::HANDSHAKE && role_ == SessionRole::HOST) {
        spdlog::info("Player '{}' connected! (protocol v{}, mod v{})",
                      hs->player_name, hs->protocol_version, hs->mod_version);

        // Send ack
        HandshakePacket ack{};
        ack.header.type = PacketType::HANDSHAKE_ACK;
        ack.header.payload_size = sizeof(HandshakePacket) - sizeof(PacketHeader);
        ack.protocol_version = 1;
        auto& cfg = get_config();
        strncpy(ack.player_name, cfg.player_name.c_str(), sizeof(ack.player_name) - 1);
        ack.mod_version = 1;
        send_packet(ack);

        state_ = SessionState::CONNECTED;

        // Spawn player 2 and apply co-op scaling
        PlayerManager::instance().spawn_remote_player();
        EnemySync::instance().apply_coop_scaling();

    } else if (hs->header.type == PacketType::HANDSHAKE_ACK && role_ == SessionRole::CLIENT) {
        spdlog::info("Connected to host '{}'!", hs->player_name);
        state_ = SessionState::CONNECTED;
    }
}

void Session::send_heartbeat() {
    PacketHeader hb{};
    hb.type = PacketType::HEARTBEAT;
    hb.payload_size = 0;
    hb.sequence = sequence_++;
    send(reinterpret_cast<const uint8_t*>(&hb), sizeof(hb));
}

} // namespace cdcoop
