/// @addtogroup grp_eda
/// @{
///
/// @file port.cpp
///
/// Source file that implements the port.

#include "eda/port/port.hpp"
#include "eda/active_object/active_object.hpp"

namespace eda
{

Port::Port(ActiveObject& active_object, app::PortList port_id)
    : m_active_object{active_object}
    , m_port_id{port_id}
{
}

bool Port::post(uint32_t event_id, uint32_t opt_data)
{
    return m_active_object.post_event(*this, event_id, opt_data);
}

bool Port::post_from_isr(uint32_t event_id, uint32_t opt_data)
{
    return m_active_object.post_event_from_isr(*this, event_id, opt_data);
}

app::PortList Port::get_id() const
{
    return m_port_id;
}

} // namespace eda

/// @}
