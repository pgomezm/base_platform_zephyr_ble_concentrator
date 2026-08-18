/// @addtogroup grp_eda
/// @{
///
/// @file active_object.cpp
///
/// Source file that implements the active object.

#include "eda/active_object/active_object.hpp"
#include "eda/port/port.hpp"
#include "utils/log/log.hpp"

LOG_MODULE_REGISTER(eda, CONFIG_APP_LOG_LEVEL);

namespace eda
{

ActiveObject::ActiveObject()
    : m_thread_data{}
    , m_thread_id{nullptr}
    , m_queue{}
    , m_task_queue_memory{}
{
    k_msgq_init(&m_queue, reinterpret_cast<char*>(m_task_queue_memory), s_queue_item_size,
                s_queue_length);
}

void ActiveObject::init_task(app::TaskPriorities priority, const char* const p_task_name,
                             k_thread_stack_t* p_stack, size_t stack_size)
{
    m_thread_id = k_thread_create(&m_thread_data, p_stack, stack_size, ActiveObject::process_events,
                                  this, nullptr, nullptr, static_cast<int>(priority), 0, K_NO_WAIT);

    k_thread_name_set(m_thread_id, p_task_name);
}

bool ActiveObject::post_event(Port& port, uint32_t event_id, uint32_t opt_data_address)
{
    Event event{&port, event_id, opt_data_address};

    // K_NO_WAIT: an active object never blocks a producer. A full queue is a
    // dropped event, which is counted rather than silently absorbed.
    const int result = k_msgq_put(&m_queue, &event, K_NO_WAIT);

    if (result != 0)
    {
        LOG_MODULE_WARN("event queue full, dropped event_id=%u", event_id);
        return false;
    }

    return true;
}

bool ActiveObject::post_event_from_isr(Port& port, uint32_t event_id, uint32_t opt_data_address)
{
    // On Zephyr `k_msgq_put()` with K_NO_WAIT is ISR-safe, so this is the same
    // call. The separate entry point documents the calling context.
    return post_event(port, event_id, opt_data_address);
}

void ActiveObject::process_events(void* p_active_object, void* p_unused1, void* p_unused2)
{
    ARG_UNUSED(p_unused1);
    ARG_UNUSED(p_unused2);

    auto* const p_self = static_cast<ActiveObject*>(p_active_object);

    Event event{};

    while (true)
    {
        // K_FOREVER: this is the one place in the design that blocks, and it
        // blocks waiting for work, which is the whole point of a thread.
        if (k_msgq_get(&p_self->m_queue, &event, K_FOREVER) == 0)
        {
            if (event.port != nullptr)
            {
                event.port->handle_event(event.event_id, event.opt_data_address);
            }
        }
    }
}

} // namespace eda

/// @}
