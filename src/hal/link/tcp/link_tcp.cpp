/// @addtogroup grp_hal_link
/// @{
///
/// @file link_tcp.cpp
///
/// Source file that implements the Link HAL over a wired network interface.
///
/// Selected by CONFIG_APP_LINK_TCP. Everything about the socket itself lives in
/// hal::link::SocketLink, which the Wi-Fi backend shares; this file is only the
/// part that differs, which is how the interface comes up.
///
/// Nothing here knows which hardware carries the traffic: an integrated MAC on
/// a Nucleo and an SPI Ethernet controller such as a W5500 on a board without
/// one both appear as the default interface, and the driver sits below the
/// network stack where application code cannot see it. Changing the hardware is
/// a devicetree overlay, not a change to this file.

#include "hal/link/socket/socket_link.hpp"

#include "config.hpp"
#include "utils/log/log.hpp"

#include <zephyr/net/net_if.h>

#if defined(CONFIG_APP_LINK_TCP_USE_DHCP)
#include <zephyr/net/dhcpv4.h>
#endif

LOG_MODULE_DEFINE(hal_link_tcp);

namespace hal::link
{
namespace
{

#if defined(CONFIG_APP_LINK_TCP_USE_DHCP)

/// How long to wait for DHCP to produce an address.
constexpr uint32_t ADDRESS_WAIT_MS = 15U * 1000U;

#endif

/// A socket link on whatever wired interface the board provides.
class TcpLink : public SocketLink
{
public:
    TcpLink()
        : SocketLink({config::LINK_TCP_SERVER_ADDR,
                      config::LINK_TCP_SERVER_PORT,
                      config::LINK_TCP_CONNECT_TIMEOUT_MS,
                      static_cast<uint8_t>(config::LINK_TCP_MAX_FRAGMENT)})
    {}

protected:
    LinkError bring_up_network() override
    {
        struct net_if* const p_iface = net_if_get_default();

        if (p_iface == nullptr)
        {
            LOG_ERROR("no network interface; check the board overlay");
            return LinkError::NOT_READY;
        }

#if defined(CONFIG_APP_LINK_TCP_USE_DHCP)
        net_dhcpv4_start(p_iface);

        if (!wait_for_ipv4_address(p_iface, ADDRESS_WAIT_MS))
        {
            LOG_ERROR("DHCP did not assign an address within %u ms", ADDRESS_WAIT_MS);
            return LinkError::CONFIG_ERROR;
        }

        LOG_INFO("address obtained over DHCP");
#else
        if (!apply_static_ipv4(p_iface,
                               config::LINK_TCP_LOCAL_IP,
                               config::LINK_TCP_NETMASK,
                               config::LINK_TCP_GATEWAY))
        {
            return LinkError::CONFIG_ERROR;
        }
#endif

        return LinkError::NO_ERROR;
    }
};

} // namespace

ILink& LinkFactory::get_instance()
{
    static TcpLink instance;

    return instance;
}

} // namespace hal::link

/// @}
