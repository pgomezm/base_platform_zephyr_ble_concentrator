/// @addtogroup grp_hal_link
/// @{
///
/// @file socket_link.cpp
///
/// Source file that implements the shared TCP socket transport.

#include "hal/link/socket/socket_link.hpp"

#include "utils/log/log.hpp"

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

#include <errno.h>

LOG_MODULE_DEFINE(hal_link_socket);

namespace hal::link
{
namespace
{

/// Polling step used while waiting for an address to appear.
constexpr uint32_t ADDRESS_POLL_MS = 250U;

} // namespace

SocketLink::SocketLink(const SocketConfig& config)
    : m_config(config), m_socket(-1), m_is_connected(false), m_downlink_callback(nullptr)
{
}

LinkError SocketLink::initialize()
{
    const LinkError result = bring_up_network();

    if (result != LinkError::NO_ERROR)
    {
        return result;
    }

    LOG_INFO("network ready, uplink server %s:%u", m_config.p_server_address,
             static_cast<unsigned>(m_config.server_port));

    return LinkError::NO_ERROR;
}

LinkError SocketLink::connect()
{
    if (m_is_connected)
    {
        return LinkError::NO_ERROR;
    }

    close_socket();

    struct sockaddr_in server = {};
    server.sin_family = AF_INET;
    server.sin_port = htons(m_config.server_port);

    if (net_addr_pton(AF_INET, m_config.p_server_address, &server.sin_addr) != 0)
    {
        LOG_ERROR("server address %s is not a valid dotted quad", m_config.p_server_address);
        return LinkError::CONFIG_ERROR;
    }

    m_socket = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (m_socket < 0)
    {
        LOG_ERROR("zsock_socket failed (%d)", errno);
        return LinkError::NOT_READY;
    }

    // Bound so a server that is not answering fails in a known time rather than
    // parking the comms thread. The state machine's retry path handles it.
    struct zsock_timeval timeout = {};
    timeout.tv_sec = m_config.connect_timeout_ms / 1000U;
    timeout.tv_usec = (m_config.connect_timeout_ms % 1000U) * 1000U;
    (void)zsock_setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    (void)zsock_setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (zsock_connect(m_socket, reinterpret_cast<struct sockaddr*>(&server), sizeof(server)) < 0)
    {
        LOG_ERROR("connect to %s:%u failed (%d)", m_config.p_server_address,
                  static_cast<unsigned>(m_config.server_port), errno);
        close_socket();
        return LinkError::CONNECT_ERROR;
    }

    m_is_connected = true;
    LOG_INFO("connected to %s:%u", m_config.p_server_address,
             static_cast<unsigned>(m_config.server_port));

    return LinkError::NO_ERROR;
}

bool SocketLink::is_connected() const
{
    return m_is_connected;
}

LinkError SocketLink::send(const uint8_t* p_data, size_t length)
{
    if (!m_is_connected)
    {
        return LinkError::CONNECT_ERROR;
    }

    if (length > get_max_payload_size())
    {
        LOG_ERROR("fragment of %u bytes exceeds the %u byte limit",
                  static_cast<unsigned>(length), get_max_payload_size());
        return LinkError::PAYLOAD_TOO_LARGE;
    }

    size_t sent = 0U;

    while (sent < length)
    {
        const ssize_t result = zsock_send(m_socket, &p_data[sent], length - sent, 0);

        if (result <= 0)
        {
            LOG_ERROR("zsock_send failed (%d)", errno);

            // A broken connection is not recoverable in place. Drop it so the
            // next connect() builds a new one instead of writing into a socket
            // the peer already closed.
            close_socket();
            m_is_connected = false;

            return LinkError::SEND_ERROR;
        }

        sent += static_cast<size_t>(result);
    }

    return LinkError::NO_ERROR;
}

void SocketLink::register_downlink_callback(DownlinkCallback callback)
{
    m_downlink_callback = callback;
}

uint8_t SocketLink::get_max_payload_size() const
{
    if (!m_is_connected)
    {
        return 0U;
    }

    // TCP imposes no such limit. Reporting the configured fragment size keeps
    // svc::comms fragmenting exactly as it does on LoRaWAN, so every variant
    // produces the same wire format and one fragmentation path has to be
    // tested rather than three.
    return m_config.max_fragment;
}

uint8_t SocketLink::get_available_payload_size() const
{
    // Identical to the ceiling. Nothing rides along in a TCP segment that the
    // application did not put there, so there is no squeeze to report.
    return get_max_payload_size();
}

uint8_t SocketLink::get_max_uplinks_per_dispatch() const
{
    // No airtime to ration and no duty cycle to respect, so a dispatch that
    // needs several fragments sends all of them in the same cycle.
    return UINT8_MAX;
}

bool SocketLink::apply_static_ipv4(struct net_if* p_iface, const char* p_ip, const char* p_netmask,
                                   const char* p_gateway)
{
    struct in_addr address;
    struct in_addr netmask;
    struct in_addr gateway;

    if ((net_addr_pton(AF_INET, p_ip, &address) != 0) ||
        (net_addr_pton(AF_INET, p_netmask, &netmask) != 0) ||
        (net_addr_pton(AF_INET, p_gateway, &gateway) != 0))
    {
        LOG_ERROR("static address, netmask or gateway is not a valid dotted quad");
        return false;
    }

    if (net_if_ipv4_addr_add(p_iface, &address, NET_ADDR_MANUAL, 0U) == nullptr)
    {
        LOG_ERROR("net_if_ipv4_addr_add failed");
        return false;
    }

    (void)net_if_ipv4_set_netmask_by_addr(p_iface, &address, &netmask);
    net_if_ipv4_set_gw(p_iface, &gateway);

    LOG_INFO("static address %s", p_ip);

    return true;
}

bool SocketLink::wait_for_ipv4_address(struct net_if* p_iface, uint32_t timeout_ms)
{
    uint32_t waited_ms = 0U;

    while (waited_ms < timeout_ms)
    {
        const struct in_addr* const p_address =
            net_if_ipv4_get_global_addr(p_iface, NET_ADDR_PREFERRED);

        if (p_address != nullptr)
        {
            // Print it. "An address was obtained" is not the useful sentence -
            // which address is, because it is what says what subnet this device
            // landed on, and therefore what the uplink server's address has to
            // be. Leaving it out sent someone to ipconfig on the wrong machine.
            char text[NET_IPV4_ADDR_LEN];

            LOG_INFO("address %s",
                     net_addr_ntop(AF_INET, p_address, text, sizeof(text)));

            return true;
        }

        k_msleep(ADDRESS_POLL_MS);
        waited_ms += ADDRESS_POLL_MS;
    }

    return false;
}

void SocketLink::close_socket()
{
    if (m_socket >= 0)
    {
        (void)zsock_close(m_socket);
        m_socket = -1;
    }
}

} // namespace hal::link

/// @}
