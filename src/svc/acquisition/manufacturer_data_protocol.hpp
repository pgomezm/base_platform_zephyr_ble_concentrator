/// @defgroup grp_svc_acquisition_protocol Manufacturer Data Protocol
///
/// Data structures for the manufacturer specific advertising data emitted by
/// the sensor endpoints.
///
/// @addtogroup grp_svc_acquisition_protocol
/// @{
///
/// @file manufacturer_data_protocol.hpp
///
/// Header file that declares the wire format of the endpoint advertisements.

#pragma once

#include <cstddef>
#include <cstdint>

namespace svc::acquisition
{

/// Sensor payload carried by the advertisement.
///
/// Mirrors `UtilsManufacturerDataPayload` in
/// `base_platform_baremetal_ble/src/utils/manufacturer_data/manufacturer_data_protocol.h`
/// **byte for byte**. The endpoint firmware owns that definition and this one
/// follows it; if it changes, this changes with it, and the static assertions
/// below stop the build until it does.
struct __attribute__((packed)) SensorPayload
{
    /// Sensor temperature, in degrees Celsius.
    int8_t sns_temperature;

    /// Sensor relative humidity, in percent.
    uint8_t sns_humidity;

    /// Sensor pressure.
    uint16_t sns_pressure;

    /// Accelerometer X raw data.
    int16_t acc_x_raw_data;

    /// Accelerometer Y raw data.
    int16_t acc_y_raw_data;

    /// Accelerometer Z raw data.
    int16_t acc_z_raw_data;

    /// Battery voltage, in millivolts.
    uint16_t battery_mv;

    /// The endpoint's own sequence number or uptime. **Not** wall-clock time.
    uint32_t timestamp;

    /// Bitfield of status flags, copied through from the endpoint's measurement.
    uint8_t status_flags;
};

static_assert(sizeof(SensorPayload) == 17U,
              "SensorPayload must match the endpoint's 17 byte payload");

/// A magnet arrived at the endpoint since its previous measurement.
///
/// A latch, not a level: the endpoint only watches the arrival edge, so a
/// magnet swiped between two measurements still reports, and one resting on the
/// device does not keep reporting.
constexpr uint8_t SENSOR_STATUS_MAGNET_PRESENT = 1U << 0U;

/// BLE AD type for Manufacturer Specific Data.
///
/// This is the *element type* of an advertising data structure, not a field of
/// any frame. Confusing the two is what made the first version of this parser
/// read the Flags element and reject every endpoint in range.
constexpr uint8_t AD_TYPE_MANUFACTURER_SPECIFIC = 0xFFU;

/// The payload the endpoints put inside their Manufacturer Specific Data
/// element.
///
/// **This is the wire format, taken from what the endpoint actually
/// transmits** — `utils_manufacturer_data_build_advertising_data()` in
/// `base_platform_baremetal_ble/src/utils/manufacturer_data/manufacturer_data.c`.
///
/// The endpoint writes the company id low byte first, and every target here is
/// little-endian, so a plain copy reads it correctly.
///
/// Note there is no device identifier anywhere in this payload. The only
/// identity an endpoint has is its BLE advertiser address, which is why
/// svc::device_table is keyed on the address. The company id is shared across
/// the whole product line: it is a filter, not an identity.
struct __attribute__((packed)) ManufacturerFrame
{
    /// Company identifier, shared by every endpoint of this product line.
    uint16_t company_id;

    /// The sensor payload.
    SensorPayload sensor_data;
};

static_assert(sizeof(ManufacturerFrame) == 19U,
              "ManufacturerFrame must stay 19 bytes: 2 of company id plus 17 of sensor data");

} // namespace svc::acquisition

/// @}
