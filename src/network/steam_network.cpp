#include <cdcoop/network/steam_network.h>
#include <cdcoop/network/session.h>
#include <spdlog/spdlog.h>

#if CDCOOP_STEAM

// Steamworks SDK headers
// Download from: https://partner.steamgames.com/
// Set STEAMWORKS_SDK_PATH in CMake to the SDK root directory.
#include <steam/steam_api.h>
#include <steam/isteamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <steam/isteammatchmaking.h>
#include <steam/isteamfriends.h>

#include <memory>
#include <string>

namespace cdcoop {

// Static pointer to the active transport so the free callback can access it.
// Only one SteamNetworkTransport instance should be active at a time.
static SteamNetworkTransport* g_active_transport = nullptr;

// Max lobby members for co-op. Host + 1 joiner = 2. Keep it small so the
// invite dialog doesn't look like "start a party game" — this is 2-player.
static constexpr int kCoopLobbyMaxMembers = 2;

struct SteamNetworkTransport::Impl {
    HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
    HSteamListenSocket listen_socket = k_HSteamListenSocket_Invalid;
    ISteamNetworkingSockets* sockets = nullptr;
    bool connected = false;
    std::string peer_name_str;

    // --- Steam lobby + invite integration ---
    // Lobby identity once CreateLobby completes. Host sets; client
    // learns it from LobbyEnter_t.
    CSteamID lobby_id;
    bool     lobby_pending = false;  // CreateLobby in flight
    bool     is_host = false;

    // STEAM_CALLBACK macro synthesises member CCallback<>s that
    // auto-register on construction. SteamAPI_RunCallbacks (pumped by
    // the game every frame) dispatches them.
    STEAM_CALLBACK(Impl, on_lobby_entered,  LobbyEnter_t);

    // Async result of CreateLobby.
    CCallResult<Impl, LobbyCreated_t> lobby_created_cb;
    void on_lobby_created(LobbyCreated_t* result, bool io_failure);
};

// Invite listener that lives for the entire mod lifetime, independent
// of any SteamNetworkTransport. A friend invites us -> this handler
// fires -> Session::join_session spins up a new transport. Without
// this, invites arriving before we open a session would be lost
// because the transport that would've listened doesn't exist yet.
struct SteamInviteListener {
    STEAM_CALLBACK(SteamInviteListener, on_join_requested,
                   GameLobbyJoinRequested_t);
};

static std::unique_ptr<SteamInviteListener> g_invite_listener;

void install_steam_invite_listener() {
    if (!g_invite_listener) {
        g_invite_listener = std::make_unique<SteamInviteListener>();
        spdlog::info("Steam: invite listener installed (accepts lobby invites "
                     "even before a session is active)");
    }
}

void remove_steam_invite_listener() {
    g_invite_listener.reset();
}

void SteamInviteListener::on_join_requested(GameLobbyJoinRequested_t* req) {
    uint64_t lobby   = req->m_steamIDLobby.ConvertToUint64();
    uint64_t inviter = req->m_steamIDFriend.ConvertToUint64();
    spdlog::info("Steam: invite clicked, lobby={}, inviter={}", lobby, inviter);

    // If we're already in a session, bail — user must leave first.
    if (Session::instance().state() != SessionState::DISCONNECTED) {
        spdlog::warn("Steam: ignoring invite — already in a session");
        return;
    }

    // Join the Steam lobby (async; LobbyEnter_t will fire), then open
    // the P2P path directly to the inviter. Using the inviter as the
    // connect target avoids a race with GetLobbyOwner, which may not
    // be ready until LobbyEnter_t completes.
    if (auto* mm = SteamMatchmaking()) {
        mm->JoinLobby(req->m_steamIDLobby);
    }
    Session::instance().join_session(std::to_string(inviter));
}

// --- Callback handlers (defined out-of-line for readability) ---

void SteamNetworkTransport::Impl::on_lobby_created(LobbyCreated_t* result, bool io_failure) {
    lobby_pending = false;
    if (io_failure || result->m_eResult != k_EResultOK) {
        spdlog::warn("Steam: CreateLobby failed (io_failure={}, result={})",
                     io_failure, static_cast<int>(result->m_eResult));
        return;
    }
    lobby_id = CSteamID(result->m_ulSteamIDLobby);
    spdlog::info("Steam: lobby created {} (host={})",
                 lobby_id.ConvertToUint64(), is_host);

    // Rich presence "connect" string — makes Steam show "Join Game"
    // in the friends list for this user. The string is passed to the
    // friend's game as the -connect_lobby command-line arg, or fires
    // GameLobbyJoinRequested_t if they're already in-game. Format is
    // intentionally the lobby id so either path works.
    char connect[64];
    snprintf(connect, sizeof(connect), "+connect_lobby %llu",
             static_cast<unsigned long long>(lobby_id.ConvertToUint64()));
    if (auto* friends = SteamFriends()) {
        friends->SetRichPresence("connect", connect);
        friends->SetRichPresence("status", "In co-op session");
    }
}

void SteamNetworkTransport::Impl::on_lobby_entered(LobbyEnter_t* info) {
    if (info->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
        spdlog::warn("Steam: LobbyEnter failed (response={})",
                     static_cast<int>(info->m_EChatRoomEnterResponse));
        return;
    }
    lobby_id = CSteamID(info->m_ulSteamIDLobby);
    spdlog::info("Steam: entered lobby {}", lobby_id.ConvertToUint64());

    // If we're the host (we created this lobby), nothing to do here.
    if (is_host) return;

    // Client side: figure out who owns the lobby and point peer_name at
    // their display name. The actual P2P connect is driven by the
    // persistent SteamInviteListener, which calls Session::join_session
    // directly when the invite is clicked.
    if (auto* mm = SteamMatchmaking()) {
        CSteamID owner = mm->GetLobbyOwner(lobby_id);
        if (owner.IsValid() && SteamFriends()) {
            peer_name_str = SteamFriends()->GetFriendPersonaName(owner);
        }
    }
}

// Connection status change callback - uses g_active_transport to update state.
// Declared as friend in steam_network.h so we can access the private Impl;
// therefore it must have external linkage (no `static`) to match the
// friend declaration. Address-taken via reinterpret_cast below.
void on_connection_status_changed(SteamNetConnectionStatusChangedCallback_t* info) {
    spdlog::info("Steam: connection status changed to {}", static_cast<int>(info->m_info.m_eState));

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
            spdlog::warn("Steam: connection lost (reason: {})", static_cast<int>(info->m_info.m_eEndReason));
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
    impl_->is_host = true;

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

    // Kick off lobby creation so the Steam overlay "Invite to game"
    // dialog has a target. Async — result lands in on_lobby_created.
    // Failure here is non-fatal: direct-Steam-ID joins still work.
    if (auto* mm = SteamMatchmaking()) {
        SteamAPICall_t call = mm->CreateLobby(k_ELobbyTypeFriendsOnly,
                                               kCoopLobbyMaxMembers);
        if (call != k_uAPICallInvalid) {
            impl_->lobby_pending = true;
            impl_->lobby_created_cb.Set(call, impl_.get(),
                                        &Impl::on_lobby_created);
            spdlog::info("Steam: CreateLobby dispatched (friends-only, max {})",
                         kCoopLobbyMaxMembers);
        } else {
            spdlog::warn("Steam: CreateLobby returned invalid handle "
                         "— invite dialog will be unavailable");
        }
    } else {
        spdlog::warn("Steam: ISteamMatchmaking unavailable "
                     "— lobby / invite dialog disabled");
    }
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

    // Leave the Steam lobby + clear rich presence so "Join Game" stops
    // showing in friends lists. Harmless no-op if no lobby.
    if (impl_->lobby_id.IsValid()) {
        if (auto* mm = SteamMatchmaking()) mm->LeaveLobby(impl_->lobby_id);
        impl_->lobby_id = CSteamID();
    }
    if (auto* friends = SteamFriends()) {
        friends->ClearRichPresence();
    }
    impl_->is_host = false;
    impl_->lobby_pending = false;

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

    // Pump the global Steam callback dispatcher so CCallResult<> for
    // our CreateLobby result and the LobbyEnter_t callback dispatch.
    // The game also pumps this per frame; harmless to double-pump.
    SteamAPI_RunCallbacks();

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
    if (!impl_->lobby_id.IsValid()) {
        if (impl_->lobby_pending) {
            spdlog::warn("Steam: invite dialog not ready yet (lobby still creating). "
                         "Try again in a moment.");
        } else {
            spdlog::warn("Steam: invite dialog unavailable — no lobby. "
                         "Call Session::host_session() first.");
        }
        return;
    }
    if (auto* friends = SteamFriends()) {
        friends->ActivateGameOverlayInviteDialog(impl_->lobby_id);
        spdlog::info("Steam: opened invite dialog for lobby {}",
                     impl_->lobby_id.ConvertToUint64());
    } else {
        spdlog::warn("Steam: ISteamFriends unavailable, cannot open invite dialog");
    }
}

void SteamNetworkTransport::on_lobby_join(uint64_t steam_id) {
    spdlog::info("Friend joined: {}", steam_id);
    impl_->connected = true;
    impl_->peer_name_str = SteamFriends()->GetFriendPersonaName(CSteamID(steam_id));
    spdlog::info("Peer name: {}", impl_->peer_name_str);
}

} // namespace cdcoop

#endif // CDCOOP_STEAM
