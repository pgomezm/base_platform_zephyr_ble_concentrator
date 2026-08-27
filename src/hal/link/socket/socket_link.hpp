/// @addtogroup grp_hal_link
/// @{
///
/// @file socket_link.hpp
///
/// Header file that declares the shared TCP socket transport.
///
/// Two backends send uplinks over a TCP socket: `tcp/` on a wired interface and
/// `wifi/` on an associated access point. Once there is an IPv4 address on an
/// interface, **everything after that is byte for byte identical** - the same
/// connect, the same partial-write loop, the same close-on-failure policy.
///
/// So the socket lives here once and each backend supplies only the part that
/// genuinely differs: how the interface comes up. That is the same argument
/// hal::link itself is built on, applied one level down. Duplicating 150 lines
/// of socket handling across two files is how one of them quietly stops
/// matching the other.

#pragma once

#include "hal/link/link.hpp"

#include <cstddef>
#include <cstdint>

struct net_if;

namespace hal::link
{

/// What a SocketLink needs to know, supplied by the backend that owns it.
///
/// Passed in rather than read from Kconfig here, so this class stays a plain
/// component with no opinion about which build it is in. Each backend has its
/// own Kconfig symbols and hands over the values.
struct SocketConfig
{
    /// IPv4 address of the uplink server, as a dotted quad.
    const char* p_server_address;

    /// TCP port of the uplink server.
    uint16_t server_port;

    /// How long a connection attempt may take before it is called a failure.
    uint32_t connect_timeout_ms;

    /// Largest uplink fragment this transport wants to be handed, in bytes.
    uint8_t max_fragment;
};

/// A link that sends uplinks over a TCP socket.
///
/// Abstract: a backend must say how the network comes up.
class SocketLink : public ILink
{
public:
    /// Construct a socket link.
    ///
    /// @param config the server and limits this link uses
    explicit SocketLink(const SocketConfig& config);

    LinkError initialize() override;

    LinkError connect() override;

    bool is_connected() const override;

    LinkError send(const uint8_t* p_data, size_t length) override;

    void register_downlink_callback(DownlinkCallback callback) override;

    uint8_t get_max_payload_size() const override;

    uint8_t get_available_payload_size() const override;

    uint8_t get_max_uplinks_per_dispatch() const override;

protected:
    /// Bring the network up to the point where it has an IPv4 address.
    ///
    /// Called once, from initialize(). Everything the two backends do
    /// differently happens inside this call and nowhere else.
    ///
    /// @return LinkError indicating success or failure
    virtual LinkError bring_up_network() = 0;

    /// Configure an interface with a static IPv4 address.
    ///
    /// @param p_iface the interface to configure
    /// @param p_ip the address, as a dotted quad
    /// @param p_netmask the netmask, as a dotted quad
    /// @param p_gateway the default gateway, as a dotted quad
    /// @return true if the address was accepted
    static bool apply_static_ipv4(struct net_if* p_iface,
                                  const char* p_ip,
                                  const char* p_netmask,
                                  const char* p_gateway);

    /// Block until an interface has an IPv4 address, or the wait times out.
    ///
    /// @param p_iface the interface to watch
    /// @param timeout_ms how long to wait
    /// @return true if an address was assigned
    static bool wait_for_ipv4_address(struct net_if* p_iface, uint32_t timeout_ms);

private:
    /// Close the socket if one is open, and forget it.
    void close_socket();

    /// The server and limits, from the backend.
    SocketConfig m_config;

    /// The open socket, or -1.
    int m_socket;

    /// Whether the socket is connected.
    bool m_is_connected;

    /// The registered downlink callback, or nullptr.
    ///
    /// Stored, but nothing delivers to it: receiving would need a reader thread
    /// parked in zsock_recv(), and what a downlink means over a stream with no
    /// framing is undecided. Accepted rather than rejected so svc::comms is
    /// written the same way on every backend. See link.md.
    DownlinkCallback m_downlink_callback;
};

} // namespace hal::link

/// @}
