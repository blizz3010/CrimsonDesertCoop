#pragma once

#include <cdcoop/network/session.h>

#if CDCOOP_STEAM

// Forward declare Steam types to avoid requiring Steamworks headers everywhere
struct SteamNetworkingIdentity;

namespace cdcoop {

// Steam P2P networking transport using ISteamNetworkingSockets
// This provides NAT traversal, encryption, and reliable/unreliable channels
class SteamNetworkTransport : public INetworkTransport {
public:
    SteamNetworkTransport();
    ~SteamNetworkTransport() override;

    bool host(uint16_t port) override;
    bool connect(const std::string& steam_id_or_ip, uint16_t port) override;
    void disconnect() override;

    bool send(const uint8_t* data, size_t size, bool reliable) override;
    void poll() override;

    bool is_connected() const override;
    std::string peer_name() const override;

    // Steam-specific: invite a friend via Steam overlay
    void invite_friend();

    // Steam callback: when a friend accepts the invite
    void on_lobby_join(uint64_t steam_id);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cdcoop

#endif // CDCOOP_STEAM
