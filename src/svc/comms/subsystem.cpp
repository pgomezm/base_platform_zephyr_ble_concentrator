/// @addtogroup grp_svc_comms
/// @{
///
/// @file subsystem.cpp
///
/// Source file that implements the comms service.

#include "svc/comms/subsystem.hpp"
#include "svc/comms/comms_protocol.hpp"

#include "app/port.hpp"
#include "app/port_list.hpp"
#include "app/tasks_priorities.hpp"
#include "config.hpp"
#include "eda/active_object/active_object.hpp"
#include "eda/port/port.hpp"
#include "eda/timer/timer.hpp"
#include "hal/link/link.hpp"
#include "hal/system/system.hpp"
#include "svc/acquisition/subsystem.hpp"
#include "svc/device_table/subsystem.hpp"
#include "utils/log/log.hpp"

#include <string.h>

LOG_MODULE_DEFINE(svc_comms);

namespace svc::comms
{
namespace
{

/// The active object that runs this service.
eda::ActiveObject s_active_object;

/// The port of this service.
Port s_port;

/// eda::Timer callback: posts DISPATCH_DUE to this service's own port.
///
/// Runs in the hal::os timer-service context (a Zephyr ISR): hands off and
/// returns, per docs/ARCHITECTURE.md section 4.
///
/// @param p_timer unused, there is only ever one dispatch timer
void on_dispatch_timer_expired(eda::Timer* p_timer)
{
    ARG_UNUSED(p_timer);

    eda::Port::send_event_from_isr(app::PortList::COMMS_PORT,
                                   static_cast<uint32_t>(Event::DISPATCH_DUE), 0);
}

/// Fires every dispatch period, once started by start_dispatch_timer().
eda::Timer s_dispatch_timer{"dispatch", config::DISPATCH_PERIOD_MS, true,
                            &on_dispatch_timer_expired};

/// Increments once per dispatch cycle.
uint16_t s_sequence = 0U;

/// Whether the first uplink since boot has been sent.
bool s_boot_uplink_sent = false;

/// Consecutive dispatch cycles that found nothing new to report.
///
/// Reset by any uplink that leaves the device, heartbeat included.
uint16_t s_quiet_cycles = 0U;

/// Working copy of the device table, filled by snapshot() at dispatch time.
///
/// Statically allocated at full table capacity: sizing it smaller would mean a
/// dispatch could silently report fewer devices than the table holds.
device_table::Entry s_snapshot[config::MAX_DEVICES];

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

    memcpy(record.address, entry.address, sizeof(record.address));
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
/// @param p_entries the entries this fragment carries, nullptr for a heartbeat
/// @param count how many entries, zero for a heartbeat
/// @param fragment_index zero-based index of this fragment
/// @param fragment_count total fragments this cycle
/// @param now_s the current uptime, in seconds
/// @param is_heartbeat whether this is a record-less liveness uplink
/// @return true if the radio accepted the fragment
bool send_fragment(const device_table::Entry* p_entries, uint8_t count, uint8_t fragment_index,
                   uint8_t fragment_count, uint32_t now_s, bool is_heartbeat)
{
    UplinkHeader header{};

    header.concentrator_id = config::CONCENTRATOR_ID;
    header.sequence = s_sequence;
    header.fragment_index = fragment_index;
    header.fragment_count = fragment_count;
    header.record_count = count;
    header.dropped_adv_reports = saturate_to_u8(acquisition::get_dropped_report_count());
    header.evicted_devices = saturate_to_u8(device_table::get_evicted_count());

    header.flags = static_cast<uint8_t>(UplinkFlags::NONE);

    if (config::REPORT_BOOT_IN_FIRST_UPLINK && !s_boot_uplink_sent)
    {
        header.flags |= static_cast<uint8_t>(UplinkFlags::BOOT);
    }

    if (is_heartbeat)
    {
        header.flags |= static_cast<uint8_t>(UplinkFlags::HEARTBEAT);
    }

    size_t offset = 0U;

    memcpy(&s_fragment_buffer[offset], &header, sizeof(header));
    offset += sizeof(header);

    for (uint8_t index = 0U; index < count; ++index)
    {
        const EndpointRecord record = to_record(p_entries[index], now_s);

        memcpy(&s_fragment_buffer[offset], &record, sizeof(record));
        offset += sizeof(record);
    }

    const hal::link::LinkError result = hal::link::LinkFactory::get_instance().send(s_fragment_buffer, offset);

    if (result != hal::link::LinkError::NO_ERROR)
    {
        LOG_ERROR("fragment %u/%u failed to send", fragment_index + 1U, fragment_count);
        return false;
    }

    // Acknowledge here, and only for what actually left the device. Clearing at
    // snapshot time instead would lose a reading every time the radio refused a
    // packet, which is precisely when losing one matters most.
    for (uint8_t index = 0U; index < count; ++index)
    {
        device_table::mark_reported(p_entries[index].address, p_entries[index].update_seq);
    }

    LOG_INFO("sent fragment %u/%u: %u records, %u bytes", fragment_index + 1U, fragment_count, count,
            static_cast<unsigned>(offset));

    return true;
}

/// Send a record-less uplink if the device has been quiet for too long.
///
/// Reporting only what changed means a room where nothing moves produces no
/// uplink at all, and at the far end that is indistinguishable from a
/// concentrator that died. A header with no records is the difference, and it
/// still carries the dropped-report and eviction counters.
void send_heartbeat_if_due()
{
    if (config::HEARTBEAT_AFTER_CYCLES == 0U)
    {
        return;
    }

    if (s_quiet_cycles < config::HEARTBEAT_AFTER_CYCLES)
    {
        LOG_INFO("nothing new to report (%u of %u quiet cycles)",
                static_cast<unsigned>(s_quiet_cycles),
                static_cast<unsigned>(config::HEARTBEAT_AFTER_CYCLES));
        return;
    }

    ++s_sequence;

    if (send_fragment(nullptr, 0U, 0U, 1U, hal::system::get_uptime_seconds(), true))
    {
        s_quiet_cycles = 0U;
        s_boot_uplink_sent = true;
    }
}

/// Build and send the uplink for this dispatch cycle.
void dispatch()
{
    if (!hal::link::LinkFactory::get_instance().is_connected())
    {
        LOG_WARNING("dispatch skipped: not joined to a network");
        return;
    }

    const uint8_t max_payload = hal::link::LinkFactory::get_instance().get_max_payload_size();

    // The data rate can leave less room than one record needs. Fragmenting into
    // pieces the radio will refuse would loop forever, so this is a wait, not a
    // retry: the next cycle may negotiate a better rate.
    if (max_payload <= sizeof(UplinkHeader))
    {
        LOG_WARNING("dispatch skipped: %u byte payload cannot hold a %u byte header plus a record",
                max_payload, static_cast<unsigned>(sizeof(UplinkHeader)));
        return;
    }

    const uint8_t records_per_fragment =
        static_cast<uint8_t>((max_payload - sizeof(UplinkHeader)) / sizeof(EndpointRecord));

    if (records_per_fragment == 0U)
    {
        LOG_WARNING("dispatch skipped: no room for a single record at this data rate");
        return;
    }

    // Only devices whose reading has not reached the network yet. A device that
    // has not advertised since its last successful uplink carries no new
    // information, and repeating it would spend a record's worth of airtime
    // restating what the far end already has.
    const size_t total_records = device_table::snapshot(s_snapshot, config::MAX_DEVICES);

    if (total_records == 0U)
    {
        ++s_quiet_cycles;
        send_heartbeat_if_due();
        return;
    }

    ++s_sequence;

    const uint32_t now_s = hal::system::get_uptime_seconds();

    const size_t fragments_needed =
        (total_records + records_per_fragment - 1U) / records_per_fragment;

    // How many transmissions this cycle is allowed to make. On LoRaWAN this is
    // one: every fragment is a separate transmission, and sending a queue of
    // them back to back is the airtime abuse that gets a node throttled. On a
    // wired transport it is effectively unlimited and the loop below runs to
    // completion exactly as it did before.
    const uint8_t max_uplinks =
        hal::link::LinkFactory::get_instance().get_max_uplinks_per_dispatch();

    const size_t fragments_allowed =
        (fragments_needed > max_uplinks) ? max_uplinks : fragments_needed;

    const uint8_t fragment_count = static_cast<uint8_t>(fragments_allowed);

    LOG_INFO("dispatch %u: %u devices pending, sending %u of %u fragments", s_sequence,
            static_cast<unsigned>(total_records), fragment_count,
            static_cast<unsigned>(fragments_needed));

    size_t sent_records = 0U;
    bool all_sent = true;

    for (uint8_t fragment_index = 0U; fragment_index < fragment_count; ++fragment_index)
    {
        const size_t remaining = total_records - sent_records;
        const uint8_t count = (remaining > records_per_fragment)
                                  ? records_per_fragment
                                  : static_cast<uint8_t>(remaining);

        if (!send_fragment(&s_snapshot[sent_records], count, fragment_index, fragment_count, now_s,
                           false))
        {
            all_sent = false;
            break;
        }

        sent_records += count;
    }

    if (sent_records > 0U)
    {
        s_quiet_cycles = 0U;
        s_boot_uplink_sent = true;
    }

    // Nothing has to remember where this cycle stopped. Whatever did not go out
    // was never acknowledged, so it is still pending in the table and the next
    // snapshot leads with it. That is why the rotation cursor this replaced is
    // gone: it approximated fairness by position, and this is exact.
    if (all_sent && (sent_records < total_records))
    {
        LOG_INFO("%u devices deferred to the next cycle",
                static_cast<unsigned>(total_records - sent_records));
    }
}

} // namespace

bool initialize()
{
    s_active_object.init_task(app::TaskPriorities::COMMS, "comms");
    s_port.init(app::PortList::COMMS_PORT, s_active_object);

    if (hal::link::LinkFactory::get_instance().initialize() != hal::link::LinkError::NO_ERROR)
    {
        return false;
    }

    LOG_INFO("comms service ready, dispatch period %u min", config::DISPATCH_PERIOD_MIN);

    return true;
}

Port& get_port()
{
    return s_port;
}

void start_dispatch_timer()
{
    s_dispatch_timer.start();
}

void stop_dispatch_timer()
{
    s_dispatch_timer.stop();
}

void Port::execute_event(uint32_t event_id, uint32_t opt_data_address)
{
    ARG_UNUSED(opt_data_address);

    switch (static_cast<Event>(event_id))
    {
    case Event::JOIN_NETWORK:
    {
        // The outcome has to travel back: app::StateMachine waits in STARTUP
        // for NETWORK_JOINED or NETWORK_JOIN_FAILED, and until this reports one
        // of them it stays there — never entering LISTENING, never starting the
        // dispatch timer, and therefore never transmitting, however well the
        // join itself went. Discarding this result is what made a device that
        // joined the network still send nothing.
        const hal::link::LinkError result = hal::link::LinkFactory::get_instance().connect();

        const app::Event outcome = (result == hal::link::LinkError::NO_ERROR)
                                       ? app::Event::NETWORK_JOINED
                                       : app::Event::NETWORK_JOIN_FAILED;

        eda::Port::send_event(app::PortList::APP_PORT, static_cast<uint32_t>(outcome), 0U);
        break;
    }

    case Event::DISPATCH_DUE:
    case Event::DISPATCH_NOW:
        dispatch();
        break;

    case Event::INVALID:
    default:
        LOG_WARNING("unhandled event id %u", event_id);
        break;
    }
}

} // namespace svc::comms

/// @}
