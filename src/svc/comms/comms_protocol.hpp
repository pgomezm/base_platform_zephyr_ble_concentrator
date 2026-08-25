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

/// One endpoint's data, relayed.
///
/// **The concentrator does not decide what any of this means.** It carries the
/// endpoint's sensor payload through byte for byte and adds only the two things
/// the endpoint cannot know about itself: how strong its signal was here, and
/// how long ago it was heard.
///
/// An earlier version dropped pressure and the three accelerometer axes to save
/// airtime. That was a decision about the *content* of the data, and content
/// decisions belong to whatever consumes the uplinks, not to a relay. Dropping
/// the axes in particular made the accelerometer unusable on the far end, which
/// is the field the industrial application cares about most.
///
/// The record is 25 bytes instead of 13. What that costs is one number: at DR3
/// a fragment carries about 9 records instead of 15, so a cycle reports fewer
/// devices. Nothing is lost by that - unreported devices stay pending and go
/// out next cycle - and it is paid for by raising the uplink allowance to 3,
/// which is still 0.13% airtime. See APP_LINK_LORA_MAX_UPLINKS_PER_DISPATCH.
struct __attribute__((packed)) EndpointRecord
{
    /// The endpoint's BLE address. Its only identity.
    uint8_t address[hal::ble::ADDRESS_SIZE];

    /// Signal strength, in dBm, measured by this concentrator.
    ///
    /// Added here, not relayed: it is a property of the link between this
    /// concentrator and that endpoint, which the endpoint has no way to report.
    int8_t rssi;

    /// How long ago this endpoint was last heard from, in seconds.
    ///
    /// Relative rather than absolute, because the concentrator has no
    /// wall clock: an absolute timestamp from a device whose epoch is its own
    /// boot would mean nothing on the far end.
    uint16_t seconds_since_seen;

    // Everything below is the endpoint's payload, unmodified. The layout
    // mirrors svc::acquisition::EddystoneSensorData field for field. It is
    // restated here rather than embedded so that the uplink wire format stays
    // readable in one place, and so that a change to the advertisement is a
    // deliberate change here too rather than a silent one.

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

    /// The endpoint's own sequence number or uptime. **Not** wall-clock time.
    uint32_t endpoint_timestamp;
};

static_assert(sizeof(EndpointRecord) == 25U,
              "EndpointRecord must stay 25 bytes: 9 added here plus the endpoint's 16");

} // namespace svc::comms

/// @}
