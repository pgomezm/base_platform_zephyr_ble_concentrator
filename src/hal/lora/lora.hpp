/// @defgroup grp_hal_lora LoRa HAL
///
/// Hardware Abstraction Layer for the LoRa radio.
///
/// @addtogroup grp_hal_lora
/// @{
///
/// @file
///
/// LoRa HAL header file.
/// This header file contains the declaration of the LoRa HAL classes and interfaces.

#pragma once

#include <cstddef>
#include <cstdint>

namespace hal::lora
{

/// Maximum downlink payload this layer will copy, in bytes.
///
/// LoRaWAN allows up to 242 bytes at the highest data rates, but a downlink to
/// this device is a command, not bulk data. Capping the copy keeps the static
/// buffer small; anything longer is truncated and reported by data_length.
constexpr size_t MAX_DOWNLINK_SIZE = 64U;

/// Enum representing possible LoRa errors
enum class LoraError : uint32_t
{
    /// Indicates that the operation was successful
    NO_ERROR,

    /// The radio device is not present or not ready. Check the wiring and the
    /// devicetree overlay before looking anywhere else.
    NOT_READY,

    /// The radio rejected the requested configuration
    CONFIG_ERROR,

    /// The network rejected the join, or the join timed out
    JOIN_ERROR,

    /// The payload was larger than the current data rate allows
    PAYLOAD_TOO_LARGE,

    /// The radio reported a failure while transmitting
    SEND_ERROR,
};

/// A downlink received from the network.
///
/// The payload is **not** parsed here, exactly as hal::ble does not parse an
/// advertising report: this layer copies bytes and reports the port and link
/// quality. Deciding what a command means is svc::comms' job.
struct Downlink
{
    /// LoRaWAN port the downlink arrived on. The port is what distinguishes one
    /// command family from another.
    uint8_t port;

    /// Received signal strength, in dBm
    int16_t rssi;

    /// Signal-to-noise ratio, in dB
    int8_t snr;

    /// Number of valid bytes in @ref data
    uint8_t data_length;

    /// Raw downlink payload
    uint8_t data[MAX_DOWNLINK_SIZE];
};

/// Callback invoked for each downlink received.
///
/// Runs in the LoRaWAN stack's own thread. The implementation must copy what it
/// needs and return: no parsing, no blocking, no allocation. Same rule as
/// hal::ble::AdvReportCallback, and for the same reason.
using DownlinkCallback = void (*)(const Downlink& downlink);

/// Interface for LoRa operations
class ILora
{
public:
    /// Initialize the LoRa radio
    ///
    /// Brings up the device from the devicetree, it does not join a network.
    ///
    /// @return LoraError indicating success or failure
    virtual LoraError initialize() = 0;

    /// Join the LoRaWAN network
    ///
    /// Blocks until the join completes or fails. Called only from the comms
    /// thread.
    ///
    /// @return LoraError indicating success or failure
    virtual LoraError join() = 0;

    /// Check whether the device has joined a network
    ///
    /// @return true if joined
    virtual bool is_joined() const = 0;

    /// Send an uplink
    ///
    /// **This method has exactly one caller: svc::comms.** It is the single
    /// writer to the radio, which is what keeps transmit order unambiguous and
    /// keeps two contexts off the same SPI bus. See docs/ARCHITECTURE.md
    /// section 4.
    ///
    /// @param p_data The payload
    /// @param length The payload length, in bytes
    /// @return LoraError indicating success or failure
    virtual LoraError send(const uint8_t* p_data, size_t length) = 0;

    /// Register the callback that receives downlinks
    ///
    /// Must be called before join(), so a downlink that arrives with the join
    /// accept is not missed.
    ///
    /// @param callback The function to call for each downlink
    virtual void register_downlink_callback(DownlinkCallback callback) = 0;

    /// Maximum application payload the current data rate allows, in bytes
    ///
    /// Queried rather than assumed, because it changes with the data rate the
    /// network negotiates. svc::comms uses it to decide how many records fit in
    /// a fragment.
    ///
    /// @return The maximum payload size in bytes, or 0 if not joined
    virtual uint8_t get_max_payload_size() const = 0;

    /// Virtual destructor
    virtual ~ILora() = default;
};

/// Factory class for LoRa management
class LoraFactory
{
public:
    /// Get the singleton instance of the LoRa radio
    ///
    /// @return Reference to the LoRa instance
    static ILora& get_instance();
};

} // namespace hal::lora

/// @}
