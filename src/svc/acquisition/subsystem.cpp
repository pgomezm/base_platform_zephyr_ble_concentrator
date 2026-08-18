/// @addtogroup grp_svc_acquisition
/// @{
///
/// @file subsystem.cpp
///
/// Source file that implements the acquisition service.

#include "svc/acquisition/subsystem.hpp"
#include "svc/acquisition/eddystone_protocol.hpp"
#include "svc/device_table/subsystem.hpp"

#include "config.hpp"
#include "eda/active_object/active_object.hpp"
#include "hal/ble/ble.hpp"
#include "hal/system/system.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <string.h>

LOG_MODULE_REGISTER(svc_acquisition, CONFIG_APP_LOG_LEVEL);

namespace svc::acquisition
{
namespace
{

/// Stack for the acquisition thread.
K_THREAD_STACK_DEFINE(s_stack, eda::ActiveObject::s_stack_size);

/// Storage for the raw advertising report pool.
///
/// Sized by Kconfig, statically allocated. This is the buffer between the BLE
/// callback and this service's thread: its depth is exactly how much burst the
/// firmware absorbs before it starts dropping reports.
char s_report_pool_buffer[config::k_adv_report_pool_size * sizeof(hal::ble::AdvReport)];

/// The raw advertising report pool.
struct k_msgq s_report_pool;

/// Reports dropped because the pool was full.
uint16_t s_dropped_report_count = 0U;

/// The active object that runs this service.
eda::ActiveObject s_active_object;

/// The port of this service.
Port s_port{s_active_object};

/// Zephyr's advertising report callback. Runs in the Bluetooth RX thread.
///
/// Copies the report into the pool and posts an event. Nothing else: no
/// parsing, no filtering beyond what a memcpy costs, no blocking. See
/// docs/ARCHITECTURE.md section 4.
///
/// @param report the advertising report
void on_adv_report(const hal::ble::AdvReport& report)
{
    if (k_msgq_put(&s_report_pool, &report, K_NO_WAIT) != 0)
    {
        // Saturating rather than wrapping: a wrapped counter would read as
        // healthy right after a burst.
        if (s_dropped_report_count < UINT16_MAX)
        {
            ++s_dropped_report_count;
        }

        return;
    }

    (void)s_port.post_from_isr(static_cast<uint32_t>(Event::ADV_REPORT_AVAILABLE));
}

/// Try to interpret a raw advertising payload as an endpoint custom frame.
///
/// @param report the raw report
/// @param out_frame where the parsed frame is written on success
/// @return true if the payload is a custom frame from a device of ours
bool parse_custom_frame(const hal::ble::AdvReport& report, EddystoneCustomFrame& out_frame)
{
    if (report.data_length < sizeof(EddystoneCustomFrame))
    {
        return false;
    }

    // Copied rather than cast in place: the advertising buffer has no alignment
    // guarantee, and reading a uint32_t straight out of it is undefined
    // behaviour on a platform that faults on unaligned access.
    memcpy(&out_frame, report.data, sizeof(EddystoneCustomFrame));

    if (out_frame.frame_type != static_cast<uint8_t>(EddystoneFrameType::CUSTOM))
    {
        return false;
    }

    // The only content filter in the receive path. Everything else advertising
    // nearby is somebody else's device.
    if (out_frame.company_id != config::k_expected_company_id)
    {
        return false;
    }

    return true;
}

/// Drain the report pool, parsing each report and recording what it holds.
void drain_report_pool()
{
    hal::ble::AdvReport report{};

    while (k_msgq_get(&s_report_pool, &report, K_NO_WAIT) == 0)
    {
        EddystoneCustomFrame frame{};

        if (!parse_custom_frame(report, frame))
        {
            continue;
        }

        device_table::Reading reading{};
        reading.temperature = frame.sensor_data.sns_temperature;
        reading.humidity = frame.sensor_data.sns_humidity;
        reading.pressure = frame.sensor_data.sns_pressure;
        reading.acc_x = frame.sensor_data.acc_x_raw_data;
        reading.acc_y = frame.sensor_data.acc_y_raw_data;
        reading.acc_z = frame.sensor_data.acc_z_raw_data;
        reading.battery_mv = frame.sensor_data.battery_mv;
        reading.endpoint_timestamp = frame.sensor_data.timestamp;

        device_table::upsert(report.address, report.rssi, reading);
    }
}

} // namespace

bool initialize()
{
    k_msgq_init(&s_report_pool, s_report_pool_buffer, sizeof(hal::ble::AdvReport),
                config::k_adv_report_pool_size);

    s_active_object.init_task(app::TaskPriorities::ACQUISITION, "acquisition", s_stack,
                              eda::ActiveObject::s_stack_size);

    if (!hal::ble::initialize())
    {
        return false;
    }

    hal::ble::register_adv_report_callback(on_adv_report);

    LOG_INF("acquisition service ready, pool depth %u", config::k_adv_report_pool_size);

    return true;
}

Port& get_port()
{
    return s_port;
}

uint16_t get_dropped_report_count()
{
    return s_dropped_report_count;
}

void Port::handle_event(uint32_t event_id, uint32_t opt_data)
{
    ARG_UNUSED(opt_data);

    switch (static_cast<Event>(event_id))
    {
    case Event::START_SCAN:
        (void)hal::ble::start_scan();
        break;

    case Event::STOP_SCAN:
        (void)hal::ble::stop_scan();
        break;

    case Event::ADV_REPORT_AVAILABLE:
        drain_report_pool();
        break;

    case Event::INVALID:
    default:
        LOG_WRN("unhandled event id %u", event_id);
        break;
    }
}

} // namespace svc::acquisition

/// @}
