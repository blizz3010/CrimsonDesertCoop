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
    std::lock_guard lock(transition_mutex_);
    if (state_ != SessionState::DISCONNECTED) {
        spdlog::warn("Cannot host: already in state {}",
                     static_cast<int>(state_.load()));
        return false;
    }

    auto& cfg = get_config();

    std::shared_ptr<INetworkTransport> t;
#if CDCOOP_STEAM
    if (cfg.use_steam_networking) {
        t = std::make_shared<SteamNetworkTransport>();
    }
#endif

    if (!t) {
        spdlog::error("No network transport available");
        return false;
    }

    t->set_packet_callback([this](PacketType type, const uint8_t* data, size_t size) {
        on_packet_received(type, data, size);
    });

    if (!t->host(cfg.port)) {
        spdlog::error("Failed to start hosting on port {}", cfg.port);
        return false;
    }

    {
        std::lock_guard tlock(transport_mutex_);
        transport_ = std::move(t);
    }
    role_ = SessionRole::HOST;
    state_ = SessionState::HOSTING;
    spdlog::info("Hosting co-op session on port {}...", cfg.port);
    spdlog::info("Waiting for player 2 to connect");

    return true;
}

bool Session::join_session(const std::string& target) {
    std::lock_guard lock(transition_mutex_);
    if (state_ != SessionState::DISCONNECTED) {
        spdlog::warn("Cannot join: already in state {}",
                     static_cast<int>(state_.load()));
        return false;
    }

    auto& cfg = get_config();

    std::shared_ptr<INetworkTransport> t;
#if CDCOOP_STEAM
    if (cfg.use_steam_networking) {
        t = std::make_shared<SteamNetworkTransport>();
    }
#endif

    if (!t) {
        spdlog::error("No network transport available");
        return false;
    }

    t->set_packet_callback([this](PacketType type, const uint8_t* data, size_t size) {
        on_packet_received(type, data, size);
    });

    if (!t->connect(target, cfg.port)) {
        spdlog::error("Failed to connect to {}", target);
        return false;
    }

    {
        std::lock_guard tlock(transport_mutex_);
        transport_ = std::move(t);
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
    std::lock_guard lock(transition_mutex_);
    if (state_ == SessionState::DISCONNECTED) return;

    spdlog::info("Leaving session...");

    // Clean up co-op state
    CompanionHijack::instance().deactivate();
    PlayerManager::instance().despawn_remote_player();
    EnemySync::instance().revert_coop_scaling();

    // Snapshot the transport, then publish nullptr so concurrent send()
    // / update() / poll() callers stop using it. Their already-held
    // shared_ptr copies keep the object alive until they return; we
    // hold our own copy here for the disconnect/cleanup sequence.
    std::shared_ptr<INetworkTransport> t;
    {
        std::lock_guard tlock(transport_mutex_);
        t = std::move(transport_);
    }

    if (t) {
        // Send disconnect packet over the captured transport directly,
        // bypassing send() since transport_ is already null.
        PacketHeader dc{};
        dc.type = PacketType::DISCONNECT;
        dc.payload_size = 0;
        if (t->is_connected()) {
            t->send(reinterpret_cast<const uint8_t*>(&dc), sizeof(dc), true);
        }
        t->disconnect();
        // t goes out of scope below — actual destruction happens once
        // every other shared_ptr copy in flight has been released.
    }

    state_ = SessionState::DISCONNECTED;
    role_ = SessionRole::NONE;
    sequence_ = 0;
    spdlog::info("Session ended");
}

void Session::send(const uint8_t* data, size_t size, bool reliable) {
    std::shared_ptr<INetworkTransport> t;
    {
        std::lock_guard lock(transport_mutex_);
        t = transport_;
    }
    if (t && t->is_connected()) {
        if (size >= sizeof(PacketHeader)) {
            // Copy packet data to stamp sequence/timestamp without mutating caller's data
            std::vector<uint8_t> buf(data, data + size);
            auto* hdr = reinterpret_cast<PacketHeader*>(buf.data());
            hdr->sequence = sequence_++;
            hdr->timestamp_ms = static_cast<uint32_t>(GetTickCount64() & 0xFFFFFFFF);
            t->send(buf.data(), buf.size(), reliable);
        } else {
            t->send(data, size, reliable);
        }
    }
}

void Session::update(float delta_time) {
    std::shared_ptr<INetworkTransport> t;
    {
        std::lock_guard lock(transport_mutex_);
        t = transport_;
    }
    if (!t) return;

    // Poll for incoming messages
    t->poll();

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
    std::shared_ptr<INetworkTransport> t;
    {
        std::lock_guard lock(transport_mutex_);
        t = transport_;
    }
    return t ? t->peer_name() : "";
}

void Session::invite_friend() {
    if (role_ != SessionRole::HOST) {
        spdlog::info("Invite: ignored — not hosting");
        return;
    }
#if CDCOOP_STEAM
    std::shared_ptr<INetworkTransport> t;
    {
        std::lock_guard lock(transport_mutex_);
        t = transport_;
    }
    // SteamNetworkTransport is the only implementation we have that
    // knows about lobbies; the abstract INetworkTransport interface
    // deliberately doesn't include invite_friend because it's a Steam-
    // specific concept. Downcast is safe here because host_session
    // only ever constructs SteamNetworkTransport when cfg.use_steam.
    if (auto* steam = dynamic_cast<SteamNetworkTransport*>(t.get())) {
        steam->invite_friend();
        return;
    }
#endif
    spdlog::warn("Invite: Steam networking disabled — share Steam ID manually");
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
