/// @defgroup grp_svc_acquisition_protocol Eddystone Protocol
///
/// Data structures for the Eddystone advertising protocol emitted by the
/// sensor endpoints.
///
/// @addtogroup grp_svc_acquisition_protocol
/// @{
///
/// @file eddystone_protocol.hpp
///
/// Header file that declares the wire format of the endpoint advertisements.

#pragma once

#include <cstddef>
#include <cstdint>

namespace svc::acquisition
{

/// Eddystone frame types.
enum class EddystoneFrameType : uint8_t
{
    UID = 0x00U,
    URL = 0x10U,
    TLM = 0x20U,
    EID = 0x30U,
    CUSTOM = 0xFFU,
};

/// Sensor payload carried by the custom frame.
///
/// Mirrors `SvcEddystoneSensorData` in
/// `base_platform_baremetal_ble/src/svc/eddystone/eddystone_protocol.h`
/// **byte for byte**. If that struct changes, this one changes with it, and the
/// static assertions below stop the build until it does.
struct __attribute__((packed)) EddystoneSensorData
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
};

static_assert(sizeof(EddystoneSensorData) == 16U,
              "EddystoneSensorData must match the endpoint's 16 byte payload");

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
/// transmits** — `build_custom_advertising_data()` in
/// `base_platform_baremetal_ble/src/svc/eddystone/eddystone.c` — and not from
/// that project's `SvcEddystoneCustomFrame`, which declares a leading
/// `frame_type` byte that the builder never emits. Mirroring the struct instead
/// of the transmission is how this went unnoticed: both sides agreed on a
/// layout neither one used.
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
    EddystoneSensorData sensor_data;
};

static_assert(sizeof(ManufacturerFrame) == 18U,
              "ManufacturerFrame must stay 18 bytes: 2 of company id plus 16 of sensor data");

} // namespace svc::acquisition

/// @}
