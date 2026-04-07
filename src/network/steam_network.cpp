#include <cdcoop/network/steam_network.h>
#include <spdlog/spdlog.h>

#if CDCOOP_STEAM

// Steamworks SDK headers
// Download from: https://partner.steamgames.com/
// Set STEAMWORKS_SDK_PATH in CMake to the SDK root directory.
#include <steam/steam_api.h>
#include <steam/isteamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>

namespace cdcoop {

// Static pointer to the active transport so the free callback can access it.
// Only one SteamNetworkTransport instance should be active at a time.
static SteamNetworkTransport* g_active_transport = nullptr;

struct SteamNetworkTransport::Impl {
    HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
    HSteamListenSocket listen_socket = k_HSteamListenSocket_Invalid;
    ISteamNetworkingSockets* sockets = nullptr;
    bool connected = false;
    std::string peer_name_str;
};

// Connection status change callback - uses g_active_transport to update state
static void on_connection_status_changed(SteamNetConnectionStatusChangedCallback_t* info) {
    spdlog::info("Steam: connection status changed to {}", info->m_info.m_eState);

    if (!g_active_transport || !g_active_transport->impl_) return;
    auto* impl = g_active_transport->impl_.get();

    switch (info->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connecting:
            // Host: accept incoming P2P connections on our listen socket
            if (impl->listen_socket != k_HSteamListenSocket_Invalid && impl->sockets) {
                spdlog::info("Steam: accepting incoming connection");
                if (impl->sockets->AcceptConnection(info->m_hConn) != k_EResultOK) {
                    spdlog::error("Steam: failed to accept connection");
                    impl->sockets->CloseConnection(info->m_hConn, 0, "AcceptFailed", false);
                }
            }
            break;

        case k_ESteamNetworkingConnectionState_Connected:
            spdlog::info("Steam: peer connected");
            impl->connection = info->m_hConn;
            impl->connected = true;
            // Try to resolve peer name via Steam Friends API
            {
                SteamNetConnectionInfo_t conn_info;
                if (impl->sockets->GetConnectionInfo(info->m_hConn, &conn_info)) {
                    CSteamID peer_id = conn_info.m_identityRemote.GetSteamID();
                    if (peer_id.IsValid()) {
                        impl->peer_name_str = SteamFriends()->GetFriendPersonaName(peer_id);
                        spdlog::info("Steam: peer name: {}", impl->peer_name_str);
                    }
                }
            }
            break;

        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            spdlog::warn("Steam: connection lost (reason: {})", info->m_info.m_eEndReason);
            if (info->m_hConn == impl->connection) {
                impl->sockets->CloseConnection(info->m_hConn, 0, nullptr, false);
                impl->connection = k_HSteamNetConnection_Invalid;
                impl->connected = false;
            }
            break;

        default:
            break;
    }
}

SteamNetworkTransport::SteamNetworkTransport() : impl_(std::make_unique<Impl>()) {
    spdlog::info("Steam network transport created");

    g_active_transport = this;

    // Initialize Steam networking interface
    impl_->sockets = SteamNetworkingSockets();
    if (!impl_->sockets) {
        spdlog::error("Failed to get ISteamNetworkingSockets interface");
        spdlog::error("Make sure Steam is running and the game was launched through Steam");
    }
}

SteamNetworkTransport::~SteamNetworkTransport() {
    disconnect();
    if (g_active_transport == this) {
        g_active_transport = nullptr;
    }
}

bool SteamNetworkTransport::host(uint16_t port) {
    if (!impl_->sockets) {
        spdlog::error("Steam: cannot host - ISteamNetworkingSockets not available");
        return false;
    }

    spdlog::info("Steam: hosting on virtual port {}", port);

    // Set up connection status callback
    SteamNetworkingConfigValue_t opt;
    opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
               reinterpret_cast<void*>(&on_connection_status_changed));

    // Create a listen socket for P2P connections
    impl_->listen_socket = impl_->sockets->CreateListenSocketP2P(port, 1, &opt);
    if (impl_->listen_socket == k_HSteamListenSocket_Invalid) {
        spdlog::error("Steam: failed to create P2P listen socket");
        return false;
    }

    spdlog::info("Steam: listening for P2P connections on virtual port {}", port);
    return true;
}

bool SteamNetworkTransport::connect(const std::string& steam_id_or_ip, uint16_t port) {
    if (!impl_->sockets) {
        spdlog::error("Steam: cannot connect - ISteamNetworkingSockets not available");
        return false;
    }

    spdlog::info("Steam: connecting to {} on port {}", steam_id_or_ip, port);

    // Parse Steam ID and create P2P connection
    SteamNetworkingIdentity identity;
    identity.SetSteamID64(std::stoull(steam_id_or_ip));

    SteamNetworkingConfigValue_t opt;
    opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
               reinterpret_cast<void*>(&on_connection_status_changed));

    impl_->connection = impl_->sockets->ConnectP2P(identity, port, 1, &opt);
    if (impl_->connection == k_HSteamNetConnection_Invalid) {
        spdlog::error("Steam: failed to create P2P connection to {}", steam_id_or_ip);
        return false;
    }

    spdlog::info("Steam: connection initiated to {}", steam_id_or_ip);
    return true;
}

void SteamNetworkTransport::disconnect() {
    if (!impl_ || !impl_->sockets) return;

    if (impl_->connection != k_HSteamNetConnection_Invalid) {
        impl_->sockets->CloseConnection(impl_->connection, 0, "Disconnect", true);
        impl_->connection = k_HSteamNetConnection_Invalid;
    }
    if (impl_->listen_socket != k_HSteamListenSocket_Invalid) {
        impl_->sockets->CloseListenSocket(impl_->listen_socket);
        impl_->listen_socket = k_HSteamListenSocket_Invalid;
    }

    impl_->connected = false;
    spdlog::info("Steam: disconnected");
}

bool SteamNetworkTransport::send(const uint8_t* data, size_t size, bool reliable) {
    if (!impl_->connected || !impl_->sockets) return false;
    if (impl_->connection == k_HSteamNetConnection_Invalid) return false;

    int flags = reliable ? k_nSteamNetworkingSend_Reliable
                         : k_nSteamNetworkingSend_Unreliable;

    auto result = impl_->sockets->SendMessageToConnection(
        impl_->connection, data, static_cast<uint32_t>(size), flags, nullptr);

    if (result != k_EResultOK) {
        spdlog::warn("Steam: send failed (result: {})", static_cast<int>(result));
        return false;
    }
    return true;
}

void SteamNetworkTransport::poll() {
    if (!impl_->sockets) return;

    // Run callbacks for connection state changes (needed for both host and client)
    impl_->sockets->RunCallbacks();

    // Poll for incoming messages
    ISteamNetworkingMessage* messages[16];
    int count = 0;

    if (impl_->connection != k_HSteamNetConnection_Invalid) {
        count = impl_->sockets->ReceiveMessagesOnConnection(
            impl_->connection, messages, 16);
    }

    for (int i = 0; i < count; ++i) {
        auto* msg = messages[i];
        if (msg->m_cbSize >= static_cast<int>(sizeof(PacketHeader)) && on_packet_) {
            if (PacketBuilder::validate(reinterpret_cast<const uint8_t*>(msg->m_pData),
                                        msg->m_cbSize)) {
                auto* header = reinterpret_cast<const PacketHeader*>(msg->m_pData);
                on_packet_(header->type, reinterpret_cast<const uint8_t*>(msg->m_pData),
                           msg->m_cbSize);
            }
        }
        msg->Release();
    }
}

bool SteamNetworkTransport::is_connected() const {
    return impl_ && impl_->connected;
}

std::string SteamNetworkTransport::peer_name() const {
    return impl_ ? impl_->peer_name_str : "";
}

void SteamNetworkTransport::invite_friend() {
    spdlog::info("Opening Steam friend invite overlay...");
    // The Steam overlay invite requires a lobby, which needs additional setup.
    // For P2P direct connections, players share Steam IDs manually.
    // SteamFriends()->ActivateGameOverlayInviteDialog(lobby_id);
}

void SteamNetworkTransport::on_lobby_join(uint64_t steam_id) {
    spdlog::info("Friend joined: {}", steam_id);
    impl_->connected = true;
    impl_->peer_name_str = SteamFriends()->GetFriendPersonaName(CSteamID(steam_id));
    spdlog::info("Peer name: {}", impl_->peer_name_str);
}

} // namespace cdcoop

#endif // CDCOOP_STEAM
