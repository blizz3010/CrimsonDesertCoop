#include <cdcoop/network/steam_network.h>
#include <spdlog/spdlog.h>

#if CDCOOP_STEAM

// NOTE: This file requires the Steamworks SDK headers.
// Download from: https://partner.steamgames.com/
// Set STEAMWORKS_SDK_PATH in CMake to the SDK root directory.

// Steamworks SDK headers would be included here:
// #include <steam/steam_api.h>
// #include <steam/isteamnetworkingsockets.h>
// #include <steam/isteamnetworkingutils.h>
// #include <steam/steamnetworkingsockets.h>

namespace cdcoop {

struct SteamNetworkTransport::Impl {
    // HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
    // HSteamListenSocket listen_socket = k_HSteamListenSocket_Invalid;
    // ISteamNetworkingSockets* sockets = nullptr;
    bool connected = false;
    std::string peer_name_str;
};

SteamNetworkTransport::SteamNetworkTransport() : impl_(std::make_unique<Impl>()) {
    spdlog::info("Steam network transport created");

    // Initialize Steam networking
    // impl_->sockets = SteamNetworkingSockets();
    // if (!impl_->sockets) {
    //     spdlog::error("Failed to get ISteamNetworkingSockets interface");
    // }
}

SteamNetworkTransport::~SteamNetworkTransport() {
    disconnect();
}

bool SteamNetworkTransport::host(uint16_t port) {
    spdlog::info("Steam: hosting on virtual port {}", port);

    // Create a listen socket for P2P connections
    // SteamNetworkingConfigValue_t opt;
    // opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, ...);
    // impl_->listen_socket = impl_->sockets->CreateListenSocketP2P(port, 1, &opt);

    // For now, also open Steam overlay for friend invites
    // invite_friend();

    // PLACEHOLDER: In production, this sets up the Steam P2P listen socket
    spdlog::warn("Steam networking is stubbed - implement with Steamworks SDK");
    return true;
}

bool SteamNetworkTransport::connect(const std::string& steam_id_or_ip, uint16_t port) {
    spdlog::info("Steam: connecting to {} on port {}", steam_id_or_ip, port);

    // Parse Steam ID and create P2P connection
    // SteamNetworkingIdentity identity;
    // identity.SetSteamID64(std::stoull(steam_id_or_ip));
    // SteamNetworkingConfigValue_t opt;
    // opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, ...);
    // impl_->connection = impl_->sockets->ConnectP2P(identity, port, 1, &opt);

    spdlog::warn("Steam networking is stubbed - implement with Steamworks SDK");
    return true;
}

void SteamNetworkTransport::disconnect() {
    if (!impl_) return;

    // if (impl_->connection != k_HSteamNetConnection_Invalid) {
    //     impl_->sockets->CloseConnection(impl_->connection, 0, "Disconnect", true);
    //     impl_->connection = k_HSteamNetConnection_Invalid;
    // }
    // if (impl_->listen_socket != k_HSteamListenSocket_Invalid) {
    //     impl_->sockets->CloseListenSocket(impl_->listen_socket);
    //     impl_->listen_socket = k_HSteamListenSocket_Invalid;
    // }

    impl_->connected = false;
    spdlog::info("Steam: disconnected");
}

bool SteamNetworkTransport::send(const uint8_t* data, size_t size, bool reliable) {
    if (!impl_->connected) return false;

    // int flags = reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable;
    // auto result = impl_->sockets->SendMessageToConnection(
    //     impl_->connection, data, static_cast<uint32_t>(size), flags, nullptr);
    // return result == k_EResultOK;

    return false; // Stubbed
}

void SteamNetworkTransport::poll() {
    // ISteamNetworkingMessage* messages[16];
    // int count = impl_->sockets->ReceiveMessagesOnConnection(
    //     impl_->connection, messages, 16);
    //
    // for (int i = 0; i < count; ++i) {
    //     auto* msg = messages[i];
    //     if (msg->m_cbSize >= sizeof(PacketHeader) && on_packet_) {
    //         auto* header = reinterpret_cast<const PacketHeader*>(msg->m_pData);
    //         on_packet_(header->type, reinterpret_cast<const uint8_t*>(msg->m_pData),
    //                    msg->m_cbSize);
    //     }
    //     msg->Release();
    // }
}

bool SteamNetworkTransport::is_connected() const {
    return impl_ && impl_->connected;
}

std::string SteamNetworkTransport::peer_name() const {
    return impl_ ? impl_->peer_name_str : "";
}

void SteamNetworkTransport::invite_friend() {
    spdlog::info("Opening Steam friend invite overlay...");
    // SteamFriends()->ActivateGameOverlayInviteDialog(lobby_id);
}

void SteamNetworkTransport::on_lobby_join(uint64_t steam_id) {
    spdlog::info("Friend joined lobby: {}", steam_id);
    impl_->connected = true;
    // impl_->peer_name_str = SteamFriends()->GetFriendPersonaName(CSteamID(steam_id));
}

} // namespace cdcoop

#endif // CDCOOP_STEAM
