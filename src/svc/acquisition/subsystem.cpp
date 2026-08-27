/// @addtogroup grp_svc_acquisition
/// @{
///
/// @file subsystem.cpp
///
/// Source file that implements the acquisition service.

#include "svc/acquisition/subsystem.hpp"
#include "svc/acquisition/eddystone_protocol.hpp"
#include "svc/device_table/subsystem.hpp"

#include "eda_config/port_list.hpp"
#include "eda_config/tasks_priorities.hpp"
#include "config.hpp"
#include "eda/active_object/active_object.hpp"
#include "eda/port/port.hpp"
#include "hal/ble/ble.hpp"
#include "hal/os/os.hpp"
#include "hal/system/system.hpp"
#include "utils/log/log.hpp"

#include <string.h>

LOG_MODULE_DEFINE(svc_acquisition);

namespace svc::acquisition
{

// Every event id addressed to this port has to be a valid index into the port's
// callback table, or execute_callback() drops it without a word.
static_assert(static_cast<uint32_t>(Event::ADV_REPORT_AVAILABLE)
                  < static_cast<uint32_t>(MAX_PORT_CALLBACKS),
              "svc::acquisition has more events than MAX_PORT_CALLBACKS allows");

namespace
{

/// Storage for the raw advertising report pool.
///
/// Sized by Kconfig, statically allocated. This is the buffer between the BLE
/// callback and this service's thread: its depth is exactly how much burst the
/// firmware absorbs before it starts dropping reports.
uint8_t s_report_pool_buffer[config::ADV_REPORT_POOL_SIZE * sizeof(hal::ble::AdvReport)];

/// The raw advertising report pool.
hal::os::Queue s_report_pool;

/// Reports dropped because the pool was full.
uint16_t s_dropped_report_count = 0U;

/// The active object that runs this service.
eda::ActiveObject s_active_object;

/// The port of this service.
Port s_port;

/// The advertising report callback. Runs in the Bluetooth stack's own receive
/// context.
///
/// Copies the report into the pool and posts an event. Nothing else: no
/// parsing, no filtering beyond what a memcpy costs, no blocking. See
/// docs/ARCHITECTURE.md section 4.
///
/// @param report the advertising report
void on_adv_report(const hal::ble::AdvReport& report)
{
    // from_isr: this runs in the Bluetooth RX thread, not an interrupt, but
    // the flag is what the interface asks for and the answer is no.
    if (!s_report_pool.put(&report, false))
    {
        // Saturating rather than wrapping: a wrapped counter would read as
        // healthy right after a burst.
        if (s_dropped_report_count < UINT16_MAX)
        {
            ++s_dropped_report_count;
        }

        return;
    }

    eda::Port::send_event_from_isr(eda_config::PortList::ACQUISITION_PORT,
                                   static_cast<uint32_t>(Event::ADV_REPORT_AVAILABLE),
                                   0);
}

/// Try to interpret a raw advertising payload as an endpoint custom frame.
///
/// @param report the raw report
/// @param out_frame where the parsed frame is written on success
/// @return true if the payload is a custom frame from a device of ours
bool parse_custom_frame(const hal::ble::AdvReport& report, ManufacturerFrame& out_frame)
{
    // Advertising data is a sequence of AD structures, each one a length byte,
    // a type byte, and the value. The endpoints emit Flags first and the
    // manufacturer data second, so the payload never starts at offset zero and
    // its offset is not fixed either. Walk the elements; do not assume a
    // layout.
    size_t offset = 0U;

    while ((offset + 1U) < report.data_length)
    {
        const uint8_t element_length = report.data[offset];

        // A zero length terminates the sequence; whatever follows is padding.
        if (element_length == 0U)
        {
            return false;
        }

        // The length counts the type byte plus the value, so this element spans
        // element_length + 1 bytes. An element claiming more than remains is
        // malformed, and is discarded rather than read past the buffer.
        if ((offset + 1U + element_length) > report.data_length)
        {
            return false;
        }

        const uint8_t element_type = report.data[offset + 1U];

        if (element_type == AD_TYPE_MANUFACTURER_SPECIFIC)
        {
            const uint8_t value_length = static_cast<uint8_t>(element_length - 1U);

            if (value_length != sizeof(ManufacturerFrame))
            {
                return false;
            }

            // Copied rather than cast in place: the advertising buffer has no
            // alignment guarantee, and reading a uint32_t straight out of it is
            // undefined behaviour on a platform that faults on unaligned
            // access.
            memcpy(&out_frame, &report.data[offset + 2U], sizeof(out_frame));

            // The only content filter in the receive path. Everything else
            // advertising nearby is somebody else's device.
            return out_frame.company_id == config::EXPECTED_COMPANY_ID;
        }

        offset += 1U + element_length;
    }

    return false;
}

/// Drain the report pool, parsing each report and recording what it holds.
void drain_report_pool()
{
    hal::ble::AdvReport report{};

    // try_get, not get: this thread was woken by its port and is draining what
    // accumulated. Blocking on the queue instead would be a second place the
    // thread waits, and an active object waits in exactly one.
    while (s_report_pool.try_get(&report))
    {
        ManufacturerFrame frame{};

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
    s_report_pool.init(s_report_pool_buffer,
                       sizeof(hal::ble::AdvReport),
                       config::ADV_REPORT_POOL_SIZE);

    s_active_object.init_task(eda_config::TaskPriorities::ACQUISITION, "acquisition");
    s_port.init(eda_config::PortList::ACQUISITION_PORT, s_active_object);

    if (hal::ble::BleFactory::get_instance().initialize() != hal::ble::BleError::NO_ERROR)
    {
        return false;
    }

    hal::ble::BleFactory::get_instance().register_adv_report_callback(on_adv_report);

    LOG_INFO("acquisition service ready, pool depth %u", config::ADV_REPORT_POOL_SIZE);

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

void Port::execute_event(uint32_t event_id, uint32_t opt_data_address)
{
    switch (static_cast<Event>(event_id))
    {
    case Event::START_SCAN:
        (void)hal::ble::BleFactory::get_instance().start_scan();
        break;

    case Event::STOP_SCAN:
        (void)hal::ble::BleFactory::get_instance().stop_scan();
        break;

    case Event::ADV_REPORT_AVAILABLE:
        drain_report_pool();
        break;

    case Event::INVALID:
    default:
        LOG_WARNING("unhandled event id %u", event_id);
        break;
    }

    // The switch above is what this service does with the event. This is how
    // another module hears about it without this service knowing it exists.
    execute_callback(event_id, opt_data_address);
}

} // namespace svc::acquisition

/// @}
