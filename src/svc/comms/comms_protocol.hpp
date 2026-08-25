/// @defgroup grp_svc_comms_protocol Uplink Protocol
///
/// The wire format of the uplink this concentrator sends.
///
/// @addtogroup grp_svc_comms_protocol
/// @{
///
/// @file comms_protocol.hpp
///
/// Header file that declares the uplink wire format.

#pragma once

#include "hal/ble/ble.hpp"

#include <cstddef>
#include <cstdint>

namespace svc::comms
{

/// Flags carried in the uplink header.
enum class UplinkFlags : uint8_t
{
    /// No flags set.
    NONE = 0x00U,

    /// This is the first uplink since the device booted.
    ///
    /// Tells whatever consumes the uplink that the device table started empty,
    /// so a gap in a device's history is a restart and not an absence. Without
    /// it the far end has to infer a reboot, which fails if it simply was not
    /// listening at the time.
    BOOT = 0x01U,

    /// This uplink carries no records: the concentrator is alive and has
    /// nothing new to report.
    ///
    /// Necessary because only devices with an unreported reading go out. A
    /// quiet room therefore produces no uplink at all, and without this flag
    /// "nothing changed" and "the concentrator is dead" look identical from the
    /// far end. `record_count` is zero and the counters in the header are still
    /// valid.
    HEARTBEAT = 0x02U,
};

/// Header at the start of every uplink fragment.
struct __attribute__((packed)) UplinkHeader
{
    /// Identifies this concentrator.
    uint32_t concentrator_id;

    /// Increments once per dispatch cycle, not per fragment. Every fragment of
    /// one cycle carries the same sequence number.
    uint16_t sequence;

    /// Zero-based index of this fragment.
    uint8_t fragment_index;

    /// Total number of fragments in this dispatch cycle.
    uint8_t fragment_count;

    /// Number of records in this fragment.
    uint8_t record_count;

    /// Advertising reports dropped since boot because the pool was full.
    uint8_t dropped_adv_reports;

    /// Devices evicted since boot because the table was full.
    uint8_t evicted_devices;

    /// UplinkFlags, or-ed together.
    uint8_t flags;
};

static_assert(sizeof(UplinkHeader) == 12U, "UplinkHeader must stay 12 bytes");

/// One endpoint's data, as reported over LoRa.
///
/// Deliberately smaller than what the endpoint advertises. Pressure and the
/// three accelerometer axes are dropped here: at the lower US915 data rates
/// every byte costs a record, and a presence and condition report does not need
/// raw accelerometer counts. Open item 5 in docs/ARCHITECTURE.md: confirm this
/// is right for whatever consumes the data.
struct __attribute__((packed)) EndpointRecord
{
    /// The endpoint's BLE address. Its only identity.
    uint8_t address[hal::ble::ADDRESS_SIZE];

    /// Signal strength, in dBm, measured by this concentrator.
    int8_t rssi;

    /// Sensor temperature, in degrees Celsius.
    int8_t temperature;

    /// Sensor relative humidity, in percent.
    uint8_t humidity;

    /// Battery voltage, in millivolts.
    uint16_t battery_mv;

    /// How long ago this endpoint was last heard from, in seconds.
    ///
    /// Relative rather than absolute, because the concentrator has no
    /// wall clock: an absolute timestamp from a device whose epoch is its own
    /// boot would mean nothing on the far end.
    uint16_t seconds_since_seen;
};

static_assert(sizeof(EndpointRecord) == 13U, "EndpointRecord must stay 13 bytes");

} // namespace svc::comms

/// @}
