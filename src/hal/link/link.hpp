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

    /// Maximum application payload the transport currently allows, in bytes
    ///
    /// Queried rather than assumed, because on LoRaWAN it changes with the data
    /// rate the network negotiates. svc::comms uses it to decide how many
    /// records fit in a fragment; a transport with no meaningful limit reports
    /// the largest fragment it wants to see and nothing has to special-case it.
    ///
    /// @return The maximum payload size in bytes, or 0 if not connected
    virtual uint8_t get_max_payload_size() const = 0;

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
