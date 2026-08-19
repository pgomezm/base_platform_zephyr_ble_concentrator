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
constexpr size_t k_address_size = 6U;

/// Maximum size of a legacy advertising payload, in bytes.
constexpr size_t k_max_adv_data_size = 31U;

/// A raw advertising report, exactly as received.
///
/// The payload is **not** parsed here. This layer copies bytes and measures
/// RSSI; interpreting them is svc::acquisition's job. That split is what keeps
/// this module usable if the endpoint's frame format ever changes.
struct AdvReport
{
    /// Advertiser address. The only identity an endpoint has, since the payload
    /// carries no device id.
    uint8_t address[k_address_size];

    /// Address type, as reported by the controller.
    uint8_t address_type;

    /// Received signal strength, in dBm. Measured by this device, not sent by
    /// the endpoint.
    int8_t rssi;

    /// Number of valid bytes in @ref data.
    uint8_t data_length;

    /// Raw advertising payload.
    uint8_t data[k_max_adv_data_size];
};

/// Callback invoked for each advertising report received.
///
/// Runs in Zephyr's Bluetooth RX thread. The implementation must copy what it
/// needs and return: no parsing, no blocking, no allocation.
using AdvReportCallback = void (*)(const AdvReport& report);

/// Initialize the BLE subsystem.
///
/// @return true if the stack came up
bool initialize();

/// Register the callback that receives advertising reports.
///
/// Must be called before start_scan().
///
/// @param callback the function to call for each report
void register_adv_report_callback(AdvReportCallback callback);

/// Start passive scanning.
///
/// @return true if scanning started
bool start_scan();

/// Stop scanning.
///
/// @return true if scanning stopped
bool stop_scan();

/// Check whether scanning is active.
///
/// @return true while scanning
bool is_scanning();

} // namespace hal::ble

/// @}
