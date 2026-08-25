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
    uint8_t address[hal::ble::ADDRESS_SIZE];

    /// Signal strength of the last advertisement, in dBm, measured here.
    int8_t rssi;

    /// The last reading received.
    Reading reading;

    /// This concentrator's uptime, in seconds, when the reading arrived.
    uint32_t last_seen_uptime_s;

    /// Increments on every reading recorded for this device.
    ///
    /// Bookkeeping, never transmitted. snapshot() copies it out and svc::comms
    /// hands it back to mark_reported() once the uplink carrying the entry has
    /// actually left the device. That round trip is what makes "delivered"
    /// race-free: a device that advertised again while the uplink was in flight
    /// has a higher sequence by the time the acknowledgement comes back, so it
    /// stays pending instead of having a fresh reading marked as already sent.
    uint16_t update_seq;

    /// The update_seq of the last reading that reached the network.
    ///
    /// Bookkeeping, never transmitted. An entry is pending exactly when this
    /// differs from update_seq.
    uint16_t reported_seq;

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

/// Copy out every fresh entry whose reading has not reached the network yet.
///
/// Returns a copy rather than a reference into the table so that the caller can
/// take as long as it needs to build and fragment an uplink without holding a
/// lock, and without blocking the acquisition thread.
///
/// A device that has not advertised since its last successful uplink is left
/// out. Repeating it would spend airtime restating something the far end
/// already knows, and with a table of last values there is nothing else it
/// could add. The cost is that a quiet room produces no uplink at all, which is
/// why svc::comms has a heartbeat.
///
/// **Called only from the comms thread.**
///
/// @param p_out where entries are written
/// @param max_entries capacity of @p p_out
/// @return the number of entries written
size_t snapshot(Entry* p_out, size_t max_entries);

/// Record that an entry's reading reached the network.
///
/// Called once per record, and only after the transport accepted the fragment
/// carrying it. Marking at snapshot time instead would lose a reading every
/// time the radio refused a packet.
///
/// @p update_seq is the value copied out by snapshot(). If the entry has moved
/// past it, a newer reading arrived while the uplink was in flight and the
/// entry is left pending on purpose.
///
/// **Called only from the comms thread.**
///
/// @param p_address the endpoint's BLE address
/// @param update_seq the update sequence the uplink actually carried
void mark_reported(const uint8_t* p_address, uint16_t update_seq);

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
