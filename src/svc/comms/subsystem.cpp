/// @addtogroup grp_svc_comms
/// @{
///
/// @file subsystem.cpp
///
/// Source file that implements the comms service.

#include "svc/comms/subsystem.hpp"
#include "svc/comms/comms_protocol.hpp"

#include "app/port_list.hpp"
#include "config.hpp"
#include "eda/active_object/active_object.hpp"
#include "eda/timer/timer.hpp"
#include "hal/lora/lora.hpp"
#include "hal/system/system.hpp"
#include "svc/acquisition/subsystem.hpp"
#include "svc/device_table/subsystem.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <cstring>

LOG_MODULE_REGISTER(svc_comms, CONFIG_APP_LOG_LEVEL);

namespace svc::comms
{
namespace
{

/// Stack for the comms thread.
K_THREAD_STACK_DEFINE(s_stack, eda::ActiveObject::s_stack_size);

/// The active object that runs this service.
eda::ActiveObject s_active_object;

/// The port of this service.
Port s_port{s_active_object};

/// Fires every dispatch period.
eda::Timer s_dispatch_timer{s_port, static_cast<uint32_t>(Event::DISPATCH_DUE)};

/// Increments once per dispatch cycle.
uint16_t s_sequence = 0U;

/// Whether the first uplink since boot has been sent.
bool s_boot_uplink_sent = false;

/// Working copy of the device table, filled by snapshot() at dispatch time.
///
/// Statically allocated at full table capacity: sizing it smaller would mean a
/// dispatch could silently report fewer devices than the table holds.
device_table::Entry s_snapshot[config::k_max_devices];

/// The fragment currently being assembled.
///
/// One buffer, reused per fragment. Sized to the largest uplink any data rate
/// accepts, so the buffer is never the reason a fragment has to be smaller.
uint8_t s_fragment_buffer[UINT8_MAX];

/// Saturate a value to what fits in a uint8_t counter field.
///
/// @param value the value to clamp
/// @return the value, or UINT8_MAX
uint8_t saturate_to_u8(uint16_t value)
{
    return (value > UINT8_MAX) ? UINT8_MAX : static_cast<uint8_t>(value);
}

/// Turn a table entry into the record that goes on the wire.
///
/// @param entry the table entry
/// @param now_s the current uptime, in seconds
/// @return the record to transmit
EndpointRecord to_record(const device_table::Entry& entry, uint32_t now_s)
{
    EndpointRecord record{};

    std::memcpy(record.address, entry.address, sizeof(record.address));
    record.rssi = entry.rssi;
    record.temperature = entry.reading.temperature;
    record.humidity = entry.reading.humidity;
    record.battery_mv = entry.reading.battery_mv;

    const uint32_t age_s = now_s - entry.last_seen_uptime_s;
    record.seconds_since_seen = (age_s > UINT16_MAX) ? UINT16_MAX : static_cast<uint16_t>(age_s);

    return record;
}

/// Build and send one fragment.
///
/// @param p_entries the entries this fragment carries
/// @param count how many entries
/// @param fragment_index zero-based index of this fragment
/// @param fragment_count total fragments this cycle
/// @param now_s the current uptime, in seconds
/// @return true if the radio accepted the fragment
bool send_fragment(const device_table::Entry* p_entries, uint8_t count, uint8_t fragment_index,
                   uint8_t fragment_count, uint32_t now_s)
{
    UplinkHeader header{};

    header.concentrator_id = config::k_concentrator_id;
    header.sequence = s_sequence;
    header.fragment_index = fragment_index;
    header.fragment_count = fragment_count;
    header.record_count = count;
    header.dropped_adv_reports = saturate_to_u8(acquisition::get_dropped_report_count());
    header.evicted_devices = saturate_to_u8(device_table::get_evicted_count());

    header.flags = static_cast<uint8_t>(UplinkFlags::NONE);

    if (config::k_report_boot_in_first_uplink && !s_boot_uplink_sent)
    {
        header.flags |= static_cast<uint8_t>(UplinkFlags::BOOT);
    }

    size_t offset = 0U;

    std::memcpy(&s_fragment_buffer[offset], &header, sizeof(header));
    offset += sizeof(header);

    for (uint8_t index = 0U; index < count; ++index)
    {
        const EndpointRecord record = to_record(p_entries[index], now_s);

        std::memcpy(&s_fragment_buffer[offset], &record, sizeof(record));
        offset += sizeof(record);
    }

    const hal::lora::Result result = hal::lora::send(s_fragment_buffer, offset);

    if (result != hal::lora::Result::OK)
    {
        LOG_ERR("fragment %u/%u failed to send", fragment_index + 1U, fragment_count);
        return false;
    }

    LOG_INF("sent fragment %u/%u: %u records, %u bytes", fragment_index + 1U, fragment_count, count,
            static_cast<unsigned>(offset));

    return true;
}

/// Build and send the uplink for this dispatch cycle.
void dispatch()
{
    if (!hal::lora::is_joined())
    {
        LOG_WRN("dispatch skipped: not joined to a network");
        return;
    }

    const uint8_t max_payload = hal::lora::get_max_payload_size();

    // The data rate can leave less room than one record needs. Fragmenting into
    // pieces the radio will refuse would loop forever, so this is a wait, not a
    // retry: the next cycle may negotiate a better rate.
    if (max_payload <= sizeof(UplinkHeader))
    {
        LOG_WRN("dispatch skipped: %u byte payload cannot hold a %u byte header plus a record",
                max_payload, static_cast<unsigned>(sizeof(UplinkHeader)));
        return;
    }

    const uint8_t records_per_fragment =
        static_cast<uint8_t>((max_payload - sizeof(UplinkHeader)) / sizeof(EndpointRecord));

    if (records_per_fragment == 0U)
    {
        LOG_WRN("dispatch skipped: no room for a single record at this data rate");
        return;
    }

    const size_t total_records = device_table::snapshot(s_snapshot, config::k_max_devices);

    if (total_records == 0U)
    {
        LOG_INF("dispatch skipped: no devices heard from this cycle");
        return;
    }

    ++s_sequence;

    const uint32_t now_s = hal::system::get_uptime_seconds();

    const size_t fragment_count_full =
        (total_records + records_per_fragment - 1U) / records_per_fragment;

    const uint8_t fragment_count = (fragment_count_full > UINT8_MAX)
                                       ? UINT8_MAX
                                       : static_cast<uint8_t>(fragment_count_full);

    LOG_INF("dispatch %u: %u records in %u fragments", s_sequence,
            static_cast<unsigned>(total_records), fragment_count);

    size_t sent_records = 0U;
    bool all_sent = true;

    for (uint8_t fragment_index = 0U; fragment_index < fragment_count; ++fragment_index)
    {
        const size_t remaining = total_records - sent_records;
        const uint8_t count = (remaining > records_per_fragment)
                                  ? records_per_fragment
                                  : static_cast<uint8_t>(remaining);

        if (!send_fragment(&s_snapshot[sent_records], count, fragment_index, fragment_count, now_s))
        {
            all_sent = false;
            break;
        }

        sent_records += count;
    }

    if (all_sent)
    {
        s_boot_uplink_sent = true;
    }
}

} // namespace

bool initialize()
{
    s_active_object.init_task(app::TaskPriorities::COMMS, "comms", s_stack,
                              eda::ActiveObject::s_stack_size);

    if (hal::lora::initialize() != hal::lora::Result::OK)
    {
        return false;
    }

    LOG_INF("comms service ready, dispatch period %u min", config::k_dispatch_period_min);

    return true;
}

Port& get_port()
{
    return s_port;
}

void start_dispatch_timer()
{
    s_dispatch_timer.start_periodic(config::k_dispatch_period_ms);
}

void stop_dispatch_timer()
{
    s_dispatch_timer.stop();
}

void Port::handle_event(uint32_t event_id, uint32_t opt_data)
{
    ARG_UNUSED(opt_data);

    switch (static_cast<Event>(event_id))
    {
    case Event::JOIN_NETWORK:
        (void)hal::lora::join();
        break;

    case Event::DISPATCH_DUE:
    case Event::DISPATCH_NOW:
        dispatch();
        break;

    case Event::INVALID:
    default:
        LOG_WRN("unhandled event id %u", event_id);
        break;
    }
}

} // namespace svc::comms

/// @}
