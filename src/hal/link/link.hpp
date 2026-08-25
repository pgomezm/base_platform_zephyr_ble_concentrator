/// @defgroup grp_hal_link Link HAL
///
/// Hardware Abstraction Layer for the uplink transport.
///
/// @addtogroup grp_hal_link
/// @{
///
/// @file
///
/// Link HAL header file.
/// This header file contains the declaration of the Link HAL classes and interfaces.
///
/// The concentrator exists in more than one variant, differing only in how the
/// collected readings leave the device: LoRaWAN on the nRF52840 build, TCP on a
/// board with a wired network. Everything above this layer — the acquisition
/// service, the device table, the state machine, the uplink wire format — is
/// identical in both, so the transport is the seam and this is it.
///
/// `svc::comms` is the only module that talks to a link.

#pragma once

#include <cstddef>
#include <cstdint>

namespace hal::link
{

/// Maximum downlink payload this layer will copy, in bytes.
///
/// A downlink to this device is a command, not bulk data. Capping the copy
/// keeps the static buffer small; anything longer is truncated and reported by
/// data_length.
constexpr size_t MAX_DOWNLINK_SIZE = 64U;

/// Enum representing possible link errors
enum class LinkError : uint32_t
{
    /// Indicates that the operation was successful
    NO_ERROR,

    /// The underlying device is not present or not ready. Check the wiring and
    /// the devicetree overlay before looking anywhere else.
    NOT_READY,

    /// The transport rejected the requested configuration
    CONFIG_ERROR,

    /// The network refused the connection, or it timed out. A rejected LoRaWAN
    /// join and a refused TCP connect are the same event to everything above
    /// this layer.
    CONNECT_ERROR,

    /// The payload was larger than the transport currently allows
    PAYLOAD_TOO_LARGE,

    /// The transport reported a failure while sending
    SEND_ERROR,
};

/// A downlink received from the network.
///
/// The payload is **not** parsed here, exactly as hal::ble does not parse an
/// advertising report: this layer copies bytes and reports where they came from
/// and how good the link was. Deciding what a command means is svc::comms' job.
struct Downlink
{
    /// Where the downlink arrived. A LoRaWAN port on the LoRa backend; the
    /// transport's own notion of a channel on any other. It is what
    /// distinguishes one command family from another.
    uint8_t port;

    /// Received signal strength, in dBm. Zero on a transport that has no such
    /// measurement.
    int16_t rssi;

    /// Signal-to-noise ratio, in dB. Zero on a transport that has no such
    /// measurement.
    int8_t snr;

    /// Number of valid bytes in @ref data
    uint8_t data_length;

    /// Raw downlink payload
    uint8_t data[MAX_DOWNLINK_SIZE];
};

/// Callback invoked for each downlink received.
///
/// Runs in whatever thread the transport's stack uses. The implementation must
/// copy what it needs and return: no parsing, no blocking, no allocation. Same
/// rule as hal::ble::AdvReportCallback, and for the same reason.
using DownlinkCallback = void (*)(const Downlink& downlink);

/// Interface for uplink transport operations
class ILink
{
public:
    /// Initialize the transport
    ///
    /// Brings up the device, it does not connect to anything.
    ///
    /// @return LinkError indicating success or failure
    virtual LinkError initialize() = 0;

    /// Connect to the network
    ///
    /// Blocks until the attempt completes or fails. Called only from the comms
    /// thread. This is the LoRaWAN join on one backend and the TCP connect on
    /// another; the difference does not reach any caller.
    ///
    /// @return LinkError indicating success or failure
    virtual LinkError connect() = 0;

    /// Check whether the transport is connected
    ///
    /// @return true if connected
    virtual bool is_connected() const = 0;

    /// Send an uplink
    ///
    /// **This method has exactly one caller: svc::comms.** It is the single
    /// writer to the transport, which is what keeps send order unambiguous and,
    /// on the LoRa backend, keeps two contexts off the same SPI bus. See
    /// docs/ARCHITECTURE.md section 4.
    ///
    /// @param p_data The payload
    /// @param length The payload length, in bytes
    /// @return LinkError indicating success or failure
    virtual LinkError send(const uint8_t* p_data, size_t length) = 0;

    /// Register the callback that receives downlinks
    ///
    /// Must be called before connect(), so a downlink that arrives with the
    /// connection itself is not missed.
    ///
    /// @param callback The function to call for each downlink
    virtual void register_downlink_callback(DownlinkCallback callback) = 0;

    /// Largest application payload this transport's current settings allow
    ///
    /// The ceiling, not the budget: what the link could carry if nothing else
    /// were competing for the packet. On LoRaWAN it is a property of the
    /// negotiated data rate and nothing else.
    ///
    /// **This is the number that says whether the link is usable at all.** If
    /// it cannot hold a header, no amount of waiting helps and svc::comms falls
    /// back to an empty frame so the network can raise the rate.
    ///
    /// @return The ceiling in bytes, or 0 if not connected
    virtual uint8_t get_max_payload_size() const = 0;

    /// Application payload the **next** transmission can actually carry
    ///
    /// At or below get_max_payload_size(), and lower whenever something outside
    /// the application's control is riding along in the same packet. On LoRaWAN
    /// that is MAC traffic: a LinkADRAns owed to the network server takes its
    /// bytes before the application gets any.
    ///
    /// **This is the number that sizes a fragment.** Confusing it with the
    /// ceiling is a real bug and it was made once: a data rate that had just
    /// been raised still read as 11 bytes for one cycle, which looked like an
    /// unusable link and triggered a probe that was not needed. A squeeze on
    /// one packet is temporary and costs a cycle; an unusable data rate is
    /// permanent until something transmits.
    ///
    /// @return What fits in the next packet, in bytes, or 0 if not connected
    virtual uint8_t get_available_payload_size() const = 0;

    /// Maximum number of uplinks this transport accepts in one dispatch cycle
    ///
    /// A dispatch that does not fit in one packet is fragmented, and on a
    /// transport with no per-packet cost svc::comms sends every fragment back
    /// to back. On LoRaWAN that is wrong: each fragment is a separate
    /// transmission, and a queue of them in a row is airtime a single node is
    /// not entitled to. The limit belongs here rather than in svc::comms
    /// because it is a property of the transport, not of the data.
    ///
    /// Deferring the fragments that do not fit is safe: the device table holds
    /// the last value per device, so an entry not sent this cycle goes out in
    /// the next one carrying a fresher reading than it would have. The cost of
    /// a low limit is latency, not data. This would not hold if the table kept
    /// history.
    ///
    /// @return The maximum uplinks per dispatch cycle, never less than 1
    virtual uint8_t get_max_uplinks_per_dispatch() const = 0;

    /// Virtual destructor
    virtual ~ILink() = default;
};

/// Factory class for link management
class LinkFactory
{
public:
    /// Get the singleton instance of the transport
    ///
    /// Which backend this returns is a build-time choice: exactly one
    /// hal/link/<transport>/ source is compiled, selected in CMakeLists.txt.
    ///
    /// @return Reference to the link instance
    static ILink& get_instance();
};

} // namespace hal::link

/// @}
