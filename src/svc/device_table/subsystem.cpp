/// @addtogroup grp_svc_device_table
/// @{
///
/// @file subsystem.cpp
///
/// Source file that implements the device table service.

#include "svc/device_table/subsystem.hpp"

#include "config.hpp"
#include "hal/system/system.hpp"
#include "utils/log/log.hpp"

#include <zephyr/kernel.h>
#include <string.h>

LOG_MODULE_DEFINE(svc_device_table);

namespace svc::device_table
{
namespace
{

/// The table itself. Statically allocated, fixed capacity.
Entry s_entries[config::MAX_DEVICES];

/// Guards the table.
///
/// The table is written by the acquisition thread and read by the comms thread,
/// so it needs a lock. It is a plain guarded structure rather than an active
/// object of its own: it has no behaviour to run on a thread, only state to
/// protect.
K_MUTEX_DEFINE(s_mutex);

/// Devices evicted because the table was full.
uint16_t s_evicted_count = 0U;

/// Compare two BLE addresses.
///
/// @param p_left first address
/// @param p_right second address
/// @return true if they are the same address
bool addresses_match(const uint8_t* p_left, const uint8_t* p_right)
{
    return memcmp(p_left, p_right, hal::ble::ADDRESS_SIZE) == 0;
}

/// Find the entry for an address.
///
/// @param p_address the address to look for
/// @return pointer to the entry, or nullptr if the address is not in the table
Entry* find_entry(const uint8_t* p_address)
{
    for (auto& entry : s_entries)
    {
        if (entry.in_use && addresses_match(entry.address, p_address))
        {
            return &entry;
        }
    }

    return nullptr;
}

/// Find a slot for a new device.
///
/// Prefers a free slot. If there is none, evicts the least recently seen
/// device, which is the closest thing to "the one least likely to still be in
/// the room".
///
/// @return pointer to a usable slot, never nullptr
Entry* claim_slot()
{
    for (auto& entry : s_entries)
    {
        if (!entry.in_use)
        {
            return &entry;
        }
    }

    Entry* p_oldest = &s_entries[0];

    for (auto& entry : s_entries)
    {
        if (entry.last_seen_uptime_s < p_oldest->last_seen_uptime_s)
        {
            p_oldest = &entry;
        }
    }

    if (s_evicted_count < UINT16_MAX)
    {
        ++s_evicted_count;
    }

    LOG_WARNING("table full, evicting the device last seen at %u s", p_oldest->last_seen_uptime_s);

    return p_oldest;
}

} // namespace

void initialize()
{
    k_mutex_lock(&s_mutex, K_FOREVER);

    for (auto& entry : s_entries)
    {
        entry = Entry{};
    }

    s_evicted_count = 0U;

    k_mutex_unlock(&s_mutex);

    LOG_INFO("device table ready, capacity %u", config::MAX_DEVICES);
}

void upsert(const uint8_t* p_address, int8_t rssi, const Reading& reading)
{
    k_mutex_lock(&s_mutex, K_FOREVER);

    Entry* p_entry = find_entry(p_address);

    if (p_entry == nullptr)
    {
        p_entry = claim_slot();

        // Wipe before reuse. A recycled slot still holds the evicted device's
        // sequence counters, and inheriting them would make the new device look
        // as though its first reading had already been reported.
        *p_entry = Entry{};

        memcpy(p_entry->address, p_address, hal::ble::ADDRESS_SIZE);
        p_entry->in_use = true;
    }

    ++p_entry->update_seq;
    p_entry->rssi = rssi;
    p_entry->reading = reading;
    p_entry->last_seen_uptime_s = hal::system::get_uptime_seconds();

    k_mutex_unlock(&s_mutex);
}

size_t snapshot(Entry* p_out, size_t max_entries)
{
    if ((p_out == nullptr) || (max_entries == 0U))
    {
        return 0U;
    }

    const uint32_t now_s = hal::system::get_uptime_seconds();
    size_t written = 0U;

    k_mutex_lock(&s_mutex, K_FOREVER);

    for (const auto& entry : s_entries)
    {
        if (written >= max_entries)
        {
            break;
        }

        if (!entry.in_use)
        {
            continue;
        }

        // A device that stopped advertising is not reported: an uplink saying a
        // sensor is present should mean it was heard from recently, not that it
        // was heard from once an hour ago.
        if ((now_s - entry.last_seen_uptime_s) > config::DEVICE_STALE_AFTER_S)
        {
            continue;
        }

        // Nothing new since the last successful uplink. Repeating the entry
        // would cost airtime to restate what the far end already has.
        if (entry.update_seq == entry.reported_seq)
        {
            continue;
        }

        p_out[written] = entry;
        ++written;
    }

    k_mutex_unlock(&s_mutex);

    return written;
}

void mark_reported(const uint8_t* p_address, uint16_t update_seq)
{
    k_mutex_lock(&s_mutex, K_FOREVER);

    Entry* const p_entry = find_entry(p_address);

    // Only when the entry has not moved on. A higher sequence means a reading
    // arrived while the uplink was in flight, and marking it delivered here
    // would drop that reading until the device next advertises. An entry that
    // vanished entirely - evicted while the uplink was in flight - is simply
    // not found, and there is nothing to mark.
    if ((p_entry != nullptr) && (p_entry->update_seq == update_seq))
    {
        p_entry->reported_seq = update_seq;
    }

    k_mutex_unlock(&s_mutex);
}

uint16_t get_device_count()
{
    uint16_t count = 0U;

    k_mutex_lock(&s_mutex, K_FOREVER);

    for (const auto& entry : s_entries)
    {
        if (entry.in_use)
        {
            ++count;
        }
    }

    k_mutex_unlock(&s_mutex);

    return count;
}

uint16_t get_evicted_count()
{
    return s_evicted_count;
}

} // namespace svc::device_table

/// @}
