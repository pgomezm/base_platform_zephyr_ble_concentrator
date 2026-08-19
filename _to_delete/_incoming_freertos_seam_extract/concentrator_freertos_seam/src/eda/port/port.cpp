/// @addtogroup grp_eda
/// @{
///
/// @file port.cpp
///
/// Source file that implements the EDA port.

#include "eda/port/port.hpp"
#include "utils/log/log.hpp"

LOG_MODULE_DECLARE(eda, CONFIG_APP_LOG_LEVEL);

namespace eda
{
namespace
{

/// Size of the port registry. `deepsight-polaris-software` hardcodes this to
/// 10; here it is sized off `app::PortList::PORT_COUNT` instead, so a port
/// added to port_list.hpp cannot silently outgrow the registry.
constexpr size_t c_port_list_size_elements = static_cast<size_t>(app::PortList::PORT_COUNT);

/// Array containing the address of all the ports successfully initialized.
Port* m_active_ports_list[c_port_list_size_elements] = {};

/// True if @p port_id is in range and has a live Port registered at that slot.
///
/// deepsight-polaris-software's send_event()/send_event_from_isr() dereference
/// the registry slot without this check; here it is added, since an
/// unregistered port id would otherwise be a null pointer dereference rather
/// than the "TODO: handle uninitialized port" the reference code intends.
bool is_registered(app::PortList port_id)
{
    return (app::PortList::INVALID_PORT != port_id)
           && (static_cast<size_t>(port_id) < c_port_list_size_elements)
           && (m_active_ports_list[static_cast<size_t>(port_id)] != nullptr)
           && (m_active_ports_list[static_cast<size_t>(port_id)]->m_active_object != nullptr);
}

} // namespace

Port::Port() : m_port_id{app::PortList::INVALID_PORT}, m_active_object{nullptr}
{
    // Initialize all callback pointers to NULL
    for (uint32_t i = 0; i < MAX_PORT_CALLBACKS; ++i)
    {
        m_event_callback[i] = nullptr;
    }
}

void Port::init(app::PortList port_id, ActiveObject& active_object)
{
    if ((app::PortList::INVALID_PORT != port_id)
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

void Port::send_event(app::PortList port_id, uint32_t event_id, uint32_t opt_data_address)
{
    if (is_registered(port_id))
    {
        m_active_ports_list[static_cast<size_t>(port_id)]->m_active_object->post_event(
            *m_active_ports_list[static_cast<size_t>(port_id)], event_id, opt_data_address);
    }
    else
    {
        // TODO: handle uninitialized port
        LOG_MODULE_WARN("send_event to unregistered port %u", static_cast<unsigned>(port_id));
    }
}

void Port::send_event_from_isr(app::PortList port_id, uint32_t event_id,
                               uint32_t opt_data_address)
{
    if (is_registered(port_id))
    {
        m_active_ports_list[static_cast<size_t>(port_id)]->m_active_object->post_event_from_isr(
            *m_active_ports_list[static_cast<size_t>(port_id)], event_id, opt_data_address);
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
