/// @addtogroup grp_eda
/// @{
///
/// @file port.cpp
///
/// Source file that implements the EDA port.

#include "eda/port/port.hpp"

#include "assert/assert.hpp"
#include "utils/fault/fault.hpp"
#include "utils/log/log.hpp"

LOG_MODULE_USE(eda);

namespace eda
{
namespace
{

/// Size of the port registry. Sized off `eda_config::PortList::PORT_COUNT` so a port
/// added to port_list.hpp cannot silently outgrow it.
constexpr size_t c_port_list_size_elements = static_cast<size_t>(eda_config::PortList::PORT_COUNT);

/// Array containing the address of all the ports successfully initialized.
Port* m_active_ports_list[c_port_list_size_elements] = {};

/// True if @p port_id is in range and has a live Port registered at that slot.
///
/// Without it, sending to a port nobody registered is a null pointer
/// dereference.
bool is_registered(eda_config::PortList port_id)
{
    return (eda_config::PortList::INVALID_PORT != port_id)
           && (static_cast<size_t>(port_id) < c_port_list_size_elements)
           && (m_active_ports_list[static_cast<size_t>(port_id)] != nullptr)
           && (m_active_ports_list[static_cast<size_t>(port_id)]->m_active_object != nullptr);
}

/// @param result a failed post
/// @return what to latch in utils::fault
utils::fault::Reason to_fault_reason(PostResult result)
{
    return (result == PostResult::PORT_NOT_READY) ? utils::fault::Reason::PORT_NOT_READY
                                                  : utils::fault::Reason::EVENT_LOST;
}

/// @param result a failed post
/// @return text for the log
const char* describe(PostResult result)
{
    return (result == PostResult::PORT_NOT_READY) ? "the port has nothing registered"
                                                  : "the port queue was full";
}

} // namespace

PostResult Port::deliver(eda_config::PortList port_id, uint32_t event_id, uint32_t opt_data_address)
{
    if (!is_registered(port_id))
    {
        return PostResult::PORT_NOT_READY;
    }

    Port* const p_port = m_active_ports_list[static_cast<size_t>(port_id)];

    return p_port->m_active_object->post_event(*p_port, event_id, opt_data_address);
}

Port::Port() : m_port_id{eda_config::PortList::INVALID_PORT}, m_active_object{nullptr}
{
    // Initialize all callback pointers to NULL
    for (uint32_t i = 0; i < MAX_PORT_CALLBACKS; ++i)
    {
        m_event_callback[i] = nullptr;
    }
}

void Port::init(eda_config::PortList port_id, ActiveObject& active_object)
{
    if ((eda_config::PortList::INVALID_PORT != port_id)
        && (static_cast<size_t>(port_id) < c_port_list_size_elements))
    {
        m_port_id = port_id;
        m_active_ports_list[static_cast<size_t>(port_id)] = this;
        m_active_object = &active_object;
    }
}

void Port::set_event_callback(uint32_t event_id, EventCallback event_callback)
{
    if (event_id < MAX_PORT_CALLBACKS)
    {
        m_event_callback[event_id] = event_callback;
    }
    else
    {
        // TODO: handle invalid event ID
        return;
    }
}

void Port::send_event(eda_config::PortList port_id, uint32_t event_id, uint32_t opt_data_address)
{
    // The result is dropped on purpose: post_event() already counted and logged
    // it. A caller that cannot tolerate losing the event uses
    // send_event_critical().
    if (deliver(port_id, event_id, opt_data_address) == PostResult::PORT_NOT_READY)
    {
        LOG_WARNING("send_event to unregistered port %u", static_cast<unsigned>(port_id));
    }
}

void Port::send_event_critical(eda_config::PortList port_id, uint32_t event_id, uint32_t opt_data_address)
{
    const PostResult result = deliver(port_id, event_id, opt_data_address);

    if (result == PostResult::OK)
    {
        return;
    }

    LOG_ERROR("critical event %u lost on port %u: %s",
              event_id,
              static_cast<unsigned>(port_id),
              describe(result));
    utils::fault::report(to_fault_reason(result));
    ASSERT_CRITICAL(false);
}

void Port::send_event_from_isr(eda_config::PortList port_id, uint32_t event_id, uint32_t opt_data_address)
{
    if (is_registered(port_id))
    {
        Port* const p_port = m_active_ports_list[static_cast<size_t>(port_id)];

        // Nothing to report from an interrupt. post_event_from_isr() counts the
        // drop and the count goes out in the uplink header.
        (void)p_port->m_active_object->post_event_from_isr(*p_port, event_id, opt_data_address);
    }
    else
    {
        // TODO: handle uninitialized port
    }
}

void Port::execute_callback(uint32_t event_id, uint32_t opt_data_address)
{
    // Bounds check to prevent array access violations
    if (event_id >= MAX_PORT_CALLBACKS)
    {
        // TODO: handle invalid event ID in execute_callback
        return;
    }

    // Check if callback is set and not null
    if (m_event_callback[event_id] != nullptr)
    {
        m_event_callback[event_id](opt_data_address);
    }
}

} // namespace eda

/// @}
