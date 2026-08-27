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
#include "hal/os/os.hpp"
#include "utils/log/log.hpp"

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

/// How long to wait for the diagnostic scan to finish.
constexpr uint32_t SCAN_WAIT_MS = 15U * 1000U;

/// Signalled by the net_mgmt handler when a diagnostic scan finishes.
hal::os::Semaphore s_scan_done;

/// How many access points the last scan found.
volatile uint16_t s_scan_results = 0;

/// Whether the configured SSID was among them.
///
/// Tracked as a flag rather than left to be read off the printed list, because
/// the list is the part that gets lost: 37 results arrive within a millisecond
/// or two and the deferred logger drops whatever does not fit. A verdict that
/// survives a flooded log is worth more than a listing that does not.
volatile bool s_scan_found_target = false;

/// Where it was, when it was found.
volatile uint8_t s_target_band = 0;
volatile uint8_t s_target_channel = 0;
volatile int8_t s_target_rssi = 0;

/// Name for a frequency band, for the log.
///
/// @param band the band from a scan result
/// @return a short human-readable name
const char* band_name(uint8_t band)
{
    if (band == WIFI_FREQ_BAND_2_4_GHZ)
    {
        return "2.4 GHz";
    }

    if (band == WIFI_FREQ_BAND_5_GHZ)
    {
        return "5 GHz";
    }

    if (band == WIFI_FREQ_BAND_6_GHZ)
    {
        return "6 GHz";
    }

    return "?";
}

/// Signalled by the net_mgmt handler when the association reports a result.
///
/// A semaphore rather than a busy poll because the result arrives as an event
/// on another thread, and there is nothing useful to do until it does.
///
/// Through hal::os rather than the kernel directly. This file is a backend of
/// a *networking* stack, which it may name; the RTOS is a separate axis, and
/// the day one of them changes the other should not have to.
hal::os::Semaphore s_associate_done;

/// Whether the association that just completed succeeded.
///
/// Written by the net_mgmt handler and read by the comms thread after the
/// semaphore is taken, which is what orders the two.
volatile bool s_associated = false;

/// Whether an association attempt is waiting on a result right now.
///
/// A disconnection means two different things depending on this. While an
/// attempt is in flight it *is* the attempt failing, and it has to end the
/// wait. Afterwards it is the link dropping during normal operation, which
/// connect() handles on its next call and which must not wake a wait nobody is
/// doing.
volatile bool s_associating = false;

/// The status code the last event carried, for the log.
volatile int s_last_status = 0;

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
void on_wifi_event(struct net_mgmt_event_callback* p_callback,
                   uint64_t event,
                   struct net_if* p_iface)
{
    (void)p_iface;

    // Scan events carry a different payload type, so they are handled before
    // anything casts p_callback->info to a wifi_status.
    if (event == NET_EVENT_WIFI_SCAN_RESULT)
    {
        const struct wifi_scan_result* const p_result =
            static_cast<const struct wifi_scan_result*>(p_callback->info);

        if (p_result != nullptr)
        {
            const size_t wanted = strlen(config::LINK_WIFI_SSID);

            if ((p_result->ssid_length == wanted)
                && (memcmp(p_result->ssid, config::LINK_WIFI_SSID, wanted) == 0))
            {
                s_scan_found_target = true;
                s_target_band = p_result->band;
                s_target_channel = p_result->channel;
                s_target_rssi = p_result->rssi;
            }

            LOG_INFO("  %-32.*s  %-8s ch %-3u  %4d dBm  security %u",
                     p_result->ssid_length,
                     reinterpret_cast<const char*>(p_result->ssid),
                     band_name(p_result->band),
                     p_result->channel,
                     p_result->rssi,
                     static_cast<unsigned>(p_result->security));

            if (s_scan_results < UINT16_MAX)
            {
                s_scan_results = static_cast<uint16_t>(s_scan_results + 1U);
            }
        }

        return;
    }

    if (event == NET_EVENT_WIFI_SCAN_DONE)
    {
        s_scan_done.give(false);
        return;
    }

    const struct wifi_status* const p_status =
        static_cast<const struct wifi_status*>(p_callback->info);

    s_last_status = (p_status != nullptr) ? p_status->status : 0;

    if (event == NET_EVENT_WIFI_CONNECT_RESULT)
    {
        s_associated = (p_status != nullptr) && (p_status->status == 0);

        if (s_associating)
        {
            s_associate_done.give(false);
        }
    }
    else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT)
    {
        s_associated = false;

        if (s_associating)
        {
            // This is the attempt failing, not a link dropping later. The
            // Espressif driver reports a refused association this way rather
            // than as a CONNECT_RESULT with a non-zero status, so waiting for
            // one is waiting for something that never arrives - which cost 27
            // of the 30 seconds this used to spend before giving up.
            s_associate_done.give(false);
        }
        else
        {
            // Reported, not acted on. Rebuilding the link is connect()'s job,
            // which the state machine's error path already drives, and a
            // transport that reconnects behind everyone's back is exactly what
            // link.md forbids.
            LOG_WARNING("disassociated from the access point");
        }
    }
}

/// A socket link over an associated Wi-Fi interface.
class WifiLink : public SocketLink
{
public:
    WifiLink()
        : SocketLink({config::LINK_WIFI_SERVER_ADDR,
                      config::LINK_WIFI_SERVER_PORT,
                      config::LINK_WIFI_CONNECT_TIMEOUT_MS,
                      static_cast<uint8_t>(config::LINK_WIFI_MAX_FRAGMENT)})
    {}

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
            // Both start empty and saturate at one: each is a one-shot "the
            // answer arrived", never a count of anything.
            s_associate_done.init(0U, 1U);
            s_scan_done.init(0U, 1U);

            net_mgmt_init_event_callback(
                &s_wifi_events,
                &on_wifi_event,
                NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT
                    | NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE);
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
        if (!apply_static_ipv4(p_iface,
                               config::LINK_WIFI_LOCAL_IP,
                               config::LINK_WIFI_NETMASK,
                               config::LINK_WIFI_GATEWAY))
        {
            return LinkError::CONFIG_ERROR;
        }
#endif

        return LinkError::NO_ERROR;
    }

private:
    /// List the access points the radio can actually see, into the log.
    ///
    /// A diagnostic, run only when an association has already failed. It answers
    /// what a status code cannot: whether the configured SSID is even on the
    /// air, on what band, and how strong.
    ///
    /// @param p_iface the Wi-Fi interface
    static void report_visible_networks(struct net_if* p_iface)
    {
        struct wifi_scan_params params = {};

        s_scan_results = 0U;
        s_scan_found_target = false;
        s_scan_done.reset();

        if (net_mgmt(NET_REQUEST_WIFI_SCAN, p_iface, &params, sizeof(params)) != 0)
        {
            LOG_ERROR("could not start a scan either; the radio is not answering");
            return;
        }

        LOG_INFO("scanning for what is in range:");

        if (!s_scan_done.take(SCAN_WAIT_MS))
        {
            LOG_ERROR("the scan did not finish within %u ms", SCAN_WAIT_MS);
            return;
        }

        if (s_scan_results == 0U)
        {
            LOG_ERROR("the scan found nothing at all, which points at the antenna "
                      "or the radio rather than at the credentials");
            return;
        }

        // The verdict, stated rather than left to be inferred from a list the
        // logger may have truncated.
        if (s_scan_found_target)
        {
            LOG_INFO("\"%s\" IS on the air: %s, channel %u, %d dBm (of %u networks seen)",
                     config::LINK_WIFI_SSID,
                     band_name(s_target_band),
                     static_cast<unsigned>(s_target_channel),
                     s_target_rssi,
                     static_cast<unsigned>(s_scan_results));
            LOG_ERROR("so the network is reachable and the association still failed: "
                      "suspect the PSK, or an AP refusing this client");
        }
        else
        {
            LOG_ERROR("\"%s\" is NOT among the %u networks this radio can see",
                      config::LINK_WIFI_SSID,
                      static_cast<unsigned>(s_scan_results));
            LOG_ERROR("that means one of: it is a 5 GHz network (this chip has no 5 GHz "
                      "radio), the name differs, or it is out of range");
        }
    }

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
        s_last_status = 0;
        s_associate_done.reset();
        s_associating = true;

        LOG_INFO("associating with \"%s\"", config::LINK_WIFI_SSID);

        const int result = net_mgmt(NET_REQUEST_WIFI_CONNECT, p_iface, &params, sizeof(params));

        if (result != 0)
        {
            LOG_ERROR("NET_REQUEST_WIFI_CONNECT failed (%d)", result);
            return LinkError::CONFIG_ERROR;
        }

        const bool answered = s_associate_done.take(ASSOCIATE_WAIT_MS);

        s_associating = false;

        if (!answered)
        {
            LOG_ERROR("no association result within %u ms; the driver reported nothing at all",
                      ASSOCIATE_WAIT_MS);
            return LinkError::CONNECT_ERROR;
        }

        if (!s_associated)
        {
            LOG_ERROR("association refused, status %d", s_last_status);

            // The status code from this driver is often a generic -1, which
            // says a great deal less than the radio itself can. A scan turns
            // "it did not work" into a list of what is actually in range, and
            // settles the two questions that look identical from here: whether
            // the network is on a band this chip has (there is no 5 GHz radio
            // in an ESP32-S3), and whether it is reachable at all.
            //
            // Only on failure. A scan costs seconds and deafens the receiver
            // while it runs, which is not something to do on a working device.
            report_visible_networks(p_iface);

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
