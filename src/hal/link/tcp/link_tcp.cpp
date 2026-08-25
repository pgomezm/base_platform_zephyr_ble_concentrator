/// @addtogroup grp_hal_link
/// @{
///
/// @file link_tcp.cpp
///
/// Source file that implements the Link HAL over a TCP socket.
///
/// Selected by CONFIG_APP_LINK_TCP. Nothing here knows which network interface
/// carries the traffic: an integrated MAC on a Nucleo and an SPI Ethernet
/// controller such as a W5500 on a board without one both appear as the default
/// interface, and the driver sits below the network stack where application
/// code cannot see it. Changing the hardware is a devicetree overlay, not a
/// change to this file.

#include "hal/link/link.hpp"

#include "config.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

#if defined(CONFIG_APP_LINK_TCP_USE_DHCP)
#include <zephyr/net/dhcpv4.h>
#endif

#include <string.h>

LOG_MODULE_REGISTER(hal_link_tcp, CONFIG_APP_LOG_LEVEL);

namespace hal::link
{
namespace
{

/// The registered downlink callback, or nullptr.
DownlinkCallback s_downlink_callback = nullptr;

// Addressing is either static or DHCP, never both, and each helper below
// belongs to exactly one of those builds. Guarding them rather than leaving
// both compiled keeps -Wunused-function honest: a helper this file stops using
// should be a warning, not noise someone learns to ignore.
#if !defined(CONFIG_APP_LINK_TCP_USE_DHCP)

/// Configure the interface with the static address from Kconfig.
///
/// @param p_iface the interface to configure
/// @return true if the address was accepted
bool apply_static_address(struct net_if* p_iface)
{
    struct in_addr address;
    struct in_addr netmask;
    struct in_addr gateway;

    if ((net_addr_pton(AF_INET, config::LINK_TCP_LOCAL_IP, &address) != 0) ||
        (net_addr_pton(AF_INET, config::LINK_TCP_NETMASK, &netmask) != 0) ||
        (net_addr_pton(AF_INET, config::LINK_TCP_GATEWAY, &gateway) != 0))
    {
        LOG_ERR("static address, netmask or gateway is not a valid dotted quad");
        return false;
    }

    if (net_if_ipv4_addr_add(p_iface, &address, NET_ADDR_MANUAL, 0U) == nullptr)
    {
        LOG_ERR("net_if_ipv4_addr_add failed");
        return false;
    }

    (void)net_if_ipv4_set_netmask_by_addr(p_iface, &address, &netmask);
    net_if_ipv4_set_gw(p_iface, &gateway);

    LOG_INF("static address %s", config::LINK_TCP_LOCAL_IP);

    return true;
}

#else // CONFIG_APP_LINK_TCP_USE_DHCP

/// Milliseconds to wait for an address to appear when DHCP is in use.
constexpr uint32_t ADDRESS_WAIT_MS = 15U * 1000U;

/// Polling step used while waiting for that address.
constexpr uint32_t ADDRESS_POLL_MS = 250U;

/// Block until the interface has an IPv4 address, or the wait times out.
///
/// @param p_iface the interface to watch
/// @return true if an address was assigned
bool wait_for_address(struct net_if* p_iface)
{
    uint32_t waited_ms = 0U;

    while (waited_ms < ADDRESS_WAIT_MS)
    {
        if (net_if_ipv4_get_global_addr(p_iface, NET_ADDR_PREFERRED) != nullptr)
        {
            return true;
        }

        k_msleep(ADDRESS_POLL_MS);
        waited_ms += ADDRESS_POLL_MS;
    }

    return false;
}

#endif // !CONFIG_APP_LINK_TCP_USE_DHCP

class TcpLink : public ILink
{
public:
    TcpLink() : m_socket(-1), m_is_connected(false) {}

    LinkError initialize() override
    {
        struct net_if* const p_iface = net_if_get_default();

        if (p_iface == nullptr)
        {
            LOG_ERR("no network interface; check the board overlay");
            return LinkError::NOT_READY;
        }

#if defined(CONFIG_APP_LINK_TCP_USE_DHCP)
        net_dhcpv4_start(p_iface);

        if (!wait_for_address(p_iface))
        {
            LOG_ERR("DHCP did not assign an address within %u ms", ADDRESS_WAIT_MS);
            return LinkError::CONFIG_ERROR;
        }

        LOG_INF("address obtained over DHCP");
#else
        if (!apply_static_address(p_iface))
        {
            return LinkError::CONFIG_ERROR;
        }
#endif

        LOG_INF("network ready, uplink server %s:%u", config::LINK_TCP_SERVER_ADDR,
                static_cast<unsigned>(config::LINK_TCP_SERVER_PORT));

        return LinkError::NO_ERROR;
    }

    LinkError connect() override
    {
        if (m_is_connected)
        {
            return LinkError::NO_ERROR;
        }

        close_socket();

        struct sockaddr_in server = {};
        server.sin_family = AF_INET;
        server.sin_port = htons(config::LINK_TCP_SERVER_PORT);

        if (net_addr_pton(AF_INET, config::LINK_TCP_SERVER_ADDR, &server.sin_addr) != 0)
        {
            LOG_ERR("server address %s is not a valid dotted quad",
                    config::LINK_TCP_SERVER_ADDR);
            return LinkError::CONFIG_ERROR;
        }

        m_socket = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (m_socket < 0)
        {
            LOG_ERR("zsock_socket failed (%d)", errno);
            return LinkError::NOT_READY;
        }

        // Bound so a server that is not answering fails in a known time rather
        // than parking the comms thread. The state machine's retry path is what
        // handles the failure.
        struct zsock_timeval timeout = {};
        timeout.tv_sec = config::LINK_TCP_CONNECT_TIMEOUT_MS / 1000;
        timeout.tv_usec = (config::LINK_TCP_CONNECT_TIMEOUT_MS % 1000) * 1000;
        (void)zsock_setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        (void)zsock_setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        if (zsock_connect(m_socket, reinterpret_cast<struct sockaddr*>(&server), sizeof(server)) <
            0)
        {
            LOG_ERR("connect to %s:%u failed (%d)", config::LINK_TCP_SERVER_ADDR,
                    static_cast<unsigned>(config::LINK_TCP_SERVER_PORT), errno);
            close_socket();
            return LinkError::CONNECT_ERROR;
        }

        m_is_connected = true;
        LOG_INF("connected to %s:%u", config::LINK_TCP_SERVER_ADDR,
                static_cast<unsigned>(config::LINK_TCP_SERVER_PORT));

        return LinkError::NO_ERROR;
    }

    bool is_connected() const override
    {
        return m_is_connected;
    }

    LinkError send(const uint8_t* p_data, size_t length) override
    {
        if (!m_is_connected)
        {
            return LinkError::CONNECT_ERROR;
        }

        if (length > get_max_payload_size())
        {
            LOG_ERR("fragment of %u bytes exceeds the %u byte limit",
                    static_cast<unsigned>(length), get_max_payload_size());
            return LinkError::PAYLOAD_TOO_LARGE;
        }

        size_t sent = 0U;

        while (sent < length)
        {
            const ssize_t result = zsock_send(m_socket, &p_data[sent], length - sent, 0);

            if (result <= 0)
            {
                LOG_ERR("zsock_send failed (%d)", errno);

                // A broken connection is not recoverable in place. Drop it so
                // the next connect() builds a new one instead of writing into a
                // socket the peer already closed.
                close_socket();
                m_is_connected = false;

                return LinkError::SEND_ERROR;
            }

            sent += static_cast<size_t>(result);
        }

        return LinkError::NO_ERROR;
    }

    void register_downlink_callback(DownlinkCallback callback) override
    {
        // Stored, but nothing delivers to it yet: receiving would need a reader
        // thread parked in zsock_recv(), and what a downlink means over TCP is
        // undecided. Registering is accepted so svc::comms is written the same
        // way on both backends; see link.md.
        s_downlink_callback = callback;
    }

    uint8_t get_max_payload_size() const override
    {
        if (!m_is_connected)
        {
            return 0U;
        }

        // TCP imposes no such limit. Reporting the configured fragment size
        // keeps svc::comms fragmenting exactly as it does on LoRaWAN, so both
        // variants produce the same wire format and only one fragmentation path
        // has to be tested.
        return static_cast<uint8_t>(config::LINK_TCP_MAX_FRAGMENT);
    }

    uint8_t get_max_uplinks_per_dispatch() const override
    {
        // No airtime to ration and no duty cycle to respect, so a dispatch
        // that needs several fragments sends all of them in the same cycle.
        return UINT8_MAX;
    }

private:
    void close_socket()
    {
        if (m_socket >= 0)
        {
            (void)zsock_close(m_socket);
            m_socket = -1;
        }
    }

    int m_socket;
    bool m_is_connected;
};

} // namespace

ILink& LinkFactory::get_instance()
{
    static TcpLink instance;

    return instance;
}

} // namespace hal::link

/// @}
