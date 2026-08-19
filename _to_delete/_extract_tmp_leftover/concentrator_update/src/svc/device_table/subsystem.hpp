/// @defgroup grp_svc_device_table Device Table Service
///
/// The table of endpoint devices seen by this concentrator.
///
/// @addtogroup grp_svc_device_table
/// @{
///
/// @file subsystem.hpp
///
/// Header file that declares the device table service.

#pragma once

#include "hal/ble/ble.hpp"

#include <cstddef>
#include <cstdint>

namespace svc::device_table
{

/// One reading from an endpoint, as parsed from its advertisement.
struct Reading
{
    /// Sensor temperature, in degrees Celsius.
    int8_t temperature;

    /// Sensor relative humidity, in percent.
    uint8_t humidity;

    /// Sensor pressure.
    uint16_t pressure;

    /// Accelerometer X raw data.
    int16_t acc_x;

    /// Accelerometer Y raw data.
    int16_t acc_y;

    /// Accelerometer Z raw data.
    int16_t acc_z;

    /// Battery voltage, in millivolts.
    uint16_t battery_mv;

    /// The endpoint's own sequence number or uptime, not wall-clock time.
    uint32_t endpoint_timestamp;
};

/// One entry in the table: an endpoint and its last known reading.
struct Entry
{
    /// The endpoint's BLE address. Its only identity.
    uint8_t address[hal::ble::k_address_size];

    /// Signal strength of the last advertisement, in dBm, measured here.
    int8_t rssi;

    /// The last reading received.
    Reading reading;

    /// This concentrator's uptime, in seconds, when the reading arrived.
    uint32_t last_seen_uptime_s;

    /// Whether this slot holds a device.
    bool in_use;
};

/// Initialize the table.
void initialize();

/// Record a reading from an endpoint.
///
/// Updates the entry for this address, or takes a free slot if the address is
/// new. If the table is full, the least recently seen entry is evicted and the
/// eviction is counted.
///
/// **Called only from the acquisition thread.**
///
/// @param p_address the endpoint's BLE address
/// @param rssi signal strength of the advertisement, in dBm
/// @param reading the parsed reading
void upsert(const uint8_t* p_address, int8_t rssi, const Reading& reading);

/// Copy out every entry that is currently fresh.
///
/// Returns a copy rather than a reference into the table so that the caller can
/// take as long as it needs to build and fragment an uplink without holding a
/// lock, and without blocking the acquisition thread.
///
/// **Called only from the comms thread.**
///
/// @param p_out where entries are written
/// @param max_entries capacity of @p p_out
/// @return the number of entries written
size_t snapshot(Entry* p_out, size_t max_entries);

/// Number of devices currently held in the table.
///
/// @return the count of occupied slots
uint16_t get_device_count();

/// Number of devices evicted because the table was full.
///
/// @return the running count, saturating at UINT16_MAX
uint16_t get_evicted_count();

} // namespace svc::device_table

/// @}
