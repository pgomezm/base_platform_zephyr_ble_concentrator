/// @defgroup grp_hal_ble BLE HAL
///
/// Hardware Abstraction Layer for BLE operations.
///
/// @addtogroup grp_hal_ble
/// @{
///
/// @file ble.hpp
///
/// Header file that declares the BLE HAL interface.

#pragma once

#include <cstddef>
#include <cstdint>

namespace hal::ble
{

/// Length of a BLE device address, in bytes.
constexpr size_t ADDRESS_SIZE = 6U;

/// Maximum size of a legacy advertising payload, in bytes.
constexpr size_t MAX_ADV_DATA_SIZE = 31U;

/// A raw advertising report, exactly as received.
///
/// The payload is **not** parsed here. This layer copies bytes and measures
/// RSSI; interpreting them is svc::acquisition's job. That split is what keeps
/// this module usable if the endpoint's frame format ever changes.
struct AdvReport
{
    /// Advertiser address. The only identity an endpoint has, since the payload
    /// carries no device id.
    uint8_t address[ADDRESS_SIZE];

    /// Address type, as reported by the controller.
    uint8_t address_type;

    /// Received signal strength, in dBm. Measured by this device, not sent by
    /// the endpoint.
    int8_t rssi;

    /// Number of valid bytes in @ref data.
    uint8_t data_length;

    /// Raw advertising payload.
    uint8_t data[MAX_ADV_DATA_SIZE];
};

/// Callback invoked for each advertising report received.
///
/// Runs in Zephyr's Bluetooth RX thread. The implementation must copy what it
/// needs and return: no parsing, no blocking, no allocation.
using AdvReportCallback = void (*)(const AdvReport& report);

/// Enum representing possible BLE errors
enum class BleError : uint32_t
{
    /// Indicates that the operation was successful
    NO_ERROR,

    /// Indicates a failure reported by the Bluetooth stack
    HARDWARE_ERROR,

    /// Indicates the subsystem was already initialized
    ALREADY_RUNNING,
};

/// Interface for BLE operations
class IBle
{
public:
    /// Initialize the BLE subsystem
    ///
    /// @return BleError indicating success or failure
    virtual BleError initialize() = 0;

    /// Register the callback that receives advertising reports
    ///
    /// Must be called before start_scan().
    ///
    /// @param callback The function to call for each report
    virtual void register_adv_report_callback(AdvReportCallback callback) = 0;

    /// Start passive scanning
    ///
    /// @return BleError indicating success or failure
    virtual BleError start_scan() = 0;

    /// Stop scanning
    ///
    /// @return BleError indicating success or failure
    virtual BleError stop_scan() = 0;

    /// Check whether scanning is active
    ///
    /// @return true while scanning
    virtual bool is_scanning() const = 0;

    /// Virtual destructor
    virtual ~IBle() = default;
};

/// Factory class for BLE management
class BleFactory
{
public:
    /// Get the singleton instance of the BLE subsystem
    ///
    /// @return Reference to the BLE instance
    static IBle& get_instance();
};

} // namespace hal::ble

/// @}
