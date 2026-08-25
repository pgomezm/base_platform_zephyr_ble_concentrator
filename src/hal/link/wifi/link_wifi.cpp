/// @addtogroup grp_hal_link
/// @{
///
/// @file link_wifi.cpp
///
/// Source file that implements the Link HAL over Wi-Fi.
///
/// Selected by CONFIG_APP_LINK_WIFI. Everything about the socket itself lives
/// in hal::link::SocketLink, which the wired backend shares; this file is only
/// the part that differs, which is associating with an access point before
/// there is an interface to get an address on.
///
/// Once associated the two backends are indistinguishable, which is the point:
/// the Wi-Fi variant is not a second TCP implementation, it is the same one
/// with a different way of getting a link layer.

#include "hal/link/socket/socket_link.hpp"

#include "config.hpp"
#include "utils/log/log.hpp"

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>

#if defined(CONFIG_APP_LINK_WIFI_USE_DHCP)
#include <zephyr/net/dhcpv4.h>
#endif

#include <string.h>

LOG_MODULE_DEFINE(hal_link_wifi);

namespace hal::link
{
namespace
{

/// How long to wait for the association attempt to report a result.
constexpr uint32_t ASSOCIATE_WAIT_MS = 30U * 1000U;

/// How long to wait for an IPv4 address once associated.
constexpr uint32_t ADDRESS_WAIT_MS = 15U * 1000U;

/// Signalled by the net_mgmt handler when the association reports a result.
///
/// A semaphore rather than a busy poll because the result arrives as an event
/// on another thread, and there is nothing useful to do until it does.
K_SEM_DEFINE(s_associate_done, 0, 1);

/// Whether the association that just completed succeeded.
///
/// Written by the net_mgmt handler and read by the comms thread after the
/// semaphore is taken, which is what orders the two.
volatile bool s_associated = false;

/// The registration record handed to the net_mgmt subsystem.
///
/// Static because it must outlive the registration.
struct net_mgmt_event_callback s_wifi_events;

/// Whether the handler above has been registered.
bool s_events_registered = false;

/// Runs in the net_mgmt event thread. Records the outcome and returns.
///
/// Nothing here parses, blocks or allocates, for the same reason the BLE
/// report callback does not: see docs/ARCHITECTURE.md section 4.
///
/// @param p_callback the registration record, which carries the event payload
/// @param event which event fired
/// @param p_iface the interface it fired on, unused
void on_wifi_event(struct net_mgmt_event_callback* p_callback, uint64_t event,
                   struct net_if* p_iface)
{
    ARG_UNUSED(p_iface);

    if (event == NET_EVENT_WIFI_CONNECT_RESULT)
    {
        const struct wifi_status* const p_status =
            static_cast<const struct wifi_status*>(p_callback->info);

        s_associated = (p_status != nullptr) && (p_status->status == 0);

        k_sem_give(&s_associate_done);
    }
    else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT)
    {
        // Reported, not acted on. Rebuilding the link is connect()'s job, which
        // the state machine's error path already drives, and a transport that
        // reconnects behind everyone's back is exactly what link.md forbids.
        LOG_WARNING("disassociated from the access point");
        s_associated = false;
    }
}

/// A socket link over an associated Wi-Fi interface.
class WifiLink : public SocketLink
{
public:
    WifiLink()
        : SocketLink({config::LINK_WIFI_SERVER_ADDR, config::LINK_WIFI_SERVER_PORT,
                      config::LINK_WIFI_CONNECT_TIMEOUT_MS,
                      static_cast<uint8_t>(config::LINK_WIFI_MAX_FRAGMENT)})
    {
    }

protected:
    LinkError bring_up_network() override
    {
        struct net_if* const p_iface = net_if_get_first_wifi();

        if (p_iface == nullptr)
        {
            LOG_ERROR("no Wi-Fi interface; check that the board supports it and CONFIG_WIFI is on");
            return LinkError::NOT_READY;
        }

        if (!s_events_registered)
        {
            net_mgmt_init_event_callback(&s_wifi_events, &on_wifi_event,
                                         NET_EVENT_WIFI_CONNECT_RESULT |
                                             NET_EVENT_WIFI_DISCONNECT_RESULT);
            net_mgmt_add_event_callback(&s_wifi_events);
            s_events_registered = true;
        }

        const LinkError associated = associate(p_iface);

        if (associated != LinkError::NO_ERROR)
        {
            return associated;
        }

#if defined(CONFIG_APP_LINK_WIFI_USE_DHCP)
        net_dhcpv4_start(p_iface);

        if (!wait_for_ipv4_address(p_iface, ADDRESS_WAIT_MS))
        {
            LOG_ERROR("DHCP did not assign an address within %u ms", ADDRESS_WAIT_MS);
            return LinkError::CONFIG_ERROR;
        }

        LOG_INFO("address obtained over DHCP");
#else
        if (!apply_static_ipv4(p_iface, config::LINK_WIFI_LOCAL_IP, config::LINK_WIFI_NETMASK,
                               config::LINK_WIFI_GATEWAY))
        {
            return LinkError::CONFIG_ERROR;
        }
#endif

        return LinkError::NO_ERROR;
    }

private:
    /// Associate with the configured access point and wait for the result.
    ///
    /// Blocking and a single attempt, exactly like the LoRaWAN join: retrying
    /// is the state machine's job, through the SOFT_ERROR path, which is why
    /// nothing here loops or backs off.
    ///
    /// @param p_iface the Wi-Fi interface
    /// @return LinkError indicating success or failure
    static LinkError associate(struct net_if* p_iface)
    {
        const size_t ssid_length = strlen(config::LINK_WIFI_SSID);
        const size_t psk_length = strlen(config::LINK_WIFI_PSK);

        if ((ssid_length == 0U) || (ssid_length > WIFI_SSID_MAX_LEN))
        {
            LOG_ERROR("SSID is empty or longer than %u characters",
                      static_cast<unsigned>(WIFI_SSID_MAX_LEN));
            return LinkError::CONFIG_ERROR;
        }

        struct wifi_connect_req_params params = {};

        params.ssid = reinterpret_cast<const uint8_t*>(config::LINK_WIFI_SSID);
        params.ssid_length = static_cast<uint8_t>(ssid_length);
        params.channel = WIFI_CHANNEL_ANY;
        params.band = WIFI_FREQ_BAND_2_4_GHZ;
        params.mfp = WIFI_MFP_OPTIONAL;

        // The driver has its own timeout as well. This one is ours, because a
        // driver that never reports anything would otherwise park the comms
        // thread for good.
        params.timeout = SYS_FOREVER_MS;

        if (psk_length == 0U)
        {
            params.security = WIFI_SECURITY_TYPE_NONE;
        }
        else
        {
            params.psk = reinterpret_cast<const uint8_t*>(config::LINK_WIFI_PSK);
            params.psk_length = static_cast<uint8_t>(psk_length);
            params.security = WIFI_SECURITY_TYPE_PSK;
        }

        s_associated = false;
        k_sem_reset(&s_associate_done);

        LOG_INFO("associating with \"%s\"", config::LINK_WIFI_SSID);

        const int result =
            net_mgmt(NET_REQUEST_WIFI_CONNECT, p_iface, &params, sizeof(params));

        if (result != 0)
        {
            LOG_ERROR("NET_REQUEST_WIFI_CONNECT failed (%d)", result);
            return LinkError::CONFIG_ERROR;
        }

        if (k_sem_take(&s_associate_done, K_MSEC(ASSOCIATE_WAIT_MS)) != 0)
        {
            LOG_ERROR("no association result within %u ms", ASSOCIATE_WAIT_MS);
            return LinkError::CONNECT_ERROR;
        }

        if (!s_associated)
        {
            // The status code says which of these it was, and the Wi-Fi
            // subsystem already logged it. Repeating it here would only be a
            // second, less informed copy.
            LOG_ERROR("association refused: wrong password, AP not found, or out of range");
            return LinkError::CONNECT_ERROR;
        }

        LOG_INFO("associated");

        return LinkError::NO_ERROR;
    }
};

} // namespace

ILink& LinkFactory::get_instance()
{
    static WifiLink instance;

    return instance;
}

} // namespace hal::link

/// @}
