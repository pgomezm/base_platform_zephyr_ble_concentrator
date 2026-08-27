/// @addtogroup grp_eda
/// @{
///
/// @file active_object.cpp
///
/// Source file that implements the active object.

#include "eda/active_object/active_object.hpp"
#include "eda/port/port.hpp"
#include "utils/log/log.hpp"

LOG_MODULE_DEFINE(eda);

namespace eda
{
namespace
{

/// See get_dropped_event_count().
uint32_t s_dropped_event_count = 0U;

} // namespace

ActiveObject::ActiveObject() : m_thread{}, m_queue{}, m_task_queue_memory{}
{
    m_queue.init(m_task_queue_memory, s_queue_item_size, s_queue_length);
}

void ActiveObject::init_task(app::TaskPriorities priority, const char* const p_task_name)
{
    m_thread.create(&ActiveObject::process_events, this, static_cast<hal::os::Priority>(priority),
                    p_task_name);
}

PostResult ActiveObject::post_event(Port& port, uint32_t event_id, uint32_t opt_data_address)
{
    // allocate storage on the heap for the event
    Event event{&port, event_id, opt_data_address};
    // post the message to the appropriate thread
    if (!m_queue.put(&event, false))
    {
        ++s_dropped_event_count;
        LOG_WARNING("event queue full, dropped event_id=%u", event_id);
        return PostResult::QUEUE_FULL;
    }

    return PostResult::OK;
}

PostResult ActiveObject::post_event_from_isr(Port& port, uint32_t event_id,
                                             uint32_t opt_data_address)
{
    // allocate storage on the heap for the event
    Event event{&port, event_id, opt_data_address};
    // post the message to the appropriate thread
    if (!m_queue.put(&event, true))
    {
        // Not logged: this runs in an interrupt. The counter is the record, and
        // it goes out in the uplink header.
        ++s_dropped_event_count;
        return PostResult::QUEUE_FULL;
    }

    return PostResult::OK;
}

void ActiveObject::process_events(void* p_active_object)
{
    // Sanity casting
    ActiveObject* const active_object = reinterpret_cast<ActiveObject*>(p_active_object);

    while (true)
    {
        Event event;
        const bool status = active_object->m_queue.get(&event);
        if (status)
        {
            if (nullptr == event.port)
            {
                // TODO handle null port
            }
            else
            {
                event.port->execute_event(event.event_id, event.opt_data_address);
            }
        }
    }
}

uint32_t get_dropped_event_count()
{
    return s_dropped_event_count;
}

} // namespace eda

/// @}
