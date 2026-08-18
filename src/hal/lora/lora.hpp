/// @defgroup grp_hal_lora LoRa HAL
///
/// Hardware Abstraction Layer for the LoRa radio.
///
/// @addtogroup grp_hal_lora
/// @{
///
/// @file lora.hpp
///
/// Header file that declares the LoRa HAL interface.

#pragma once

#include <cstddef>
#include <cstdint>

namespace hal::lora
{

/// Result of a LoRa operation.
enum class Result : uint8_t
{
    /// The operation succeeded.
    OK,

    /// The radio device is not present or not ready. Check the wiring and the
    /// devicetree overlay before looking anywhere else.
    NOT_READY,

    /// The radio rejected the requested configuration.
    CONFIG_ERROR,

    /// The network rejected the join, or the join timed out.
    JOIN_ERROR,

    /// The payload was larger than the current data rate allows.
    PAYLOAD_TOO_LARGE,

    /// The radio reported a failure while transmitting.
    SEND_ERROR,
};

/// Initialize the LoRa radio.
///
/// Brings up the device from the devicetree, it does not join a network.
///
/// @return Result::OK if the radio is ready
Result initialize();

/// Join the LoRaWAN network.
///
/// Blocks until the join completes or fails. Called only from the comms thread.
///
/// @return Result::OK once joined
Result join();

/// Check whether the device has joined a network.
///
/// @return true if joined
bool is_joined();

/// Send an uplink.
///
/// **This function has exactly one caller: svc::comms.** It is the single
/// writer to the radio, which is what keeps transmit order unambiguous and
/// keeps two contexts off the same SPI bus. See docs/ARCHITECTURE.md section 4.
///
/// @param p_data the payload
/// @param length the payload length, in bytes
/// @return Result::OK if the payload was accepted for transmission
Result send(const uint8_t* p_data, size_t length);

/// Maximum application payload the current data rate allows, in bytes.
///
/// Queried rather than assumed, because it changes with the data rate the
/// network negotiates. svc::comms uses it to decide how many records fit in a
/// fragment.
///
/// @return the maximum payload size in bytes, or 0 if not joined
uint8_t get_max_payload_size();

} // namespace hal::lora

/// @}
