/// @addtogroup grp_eda
/// @{
///
/// @file port.hpp
///
/// Header file that declares the port, the entry point of an active object.

#pragma once

#include "app/port_list.hpp"

#include <cstdint>

namespace eda
{
class ActiveObject; // Forward declaration

/// A Port is how one module reaches another.
///
/// A module never calls another module's functions directly for anything
/// asynchronous: it posts an event to that module's port, and the event is
/// handled later in the owning active object's thread context. This is what
/// keeps the execution contexts in docs/ARCHITECTURE.md section 4 honest.
class Port
{
public:
    /// Constructor
    ///
    /// @param active_object the active object whose thread will run this port's handler
    /// @param port_id the identifier of this port, from app::PortList
    Port(ActiveObject& active_object, app::PortList port_id);

    /// Virtual destructor
    virtual ~Port() = default;

    // Ports are referenced by address from queued events, so they must not be
    // copied or moved after construction.
    Port(const Port&) = delete;
    Port& operator=(const Port&) = delete;
    Port(Port&&) = delete;
    Port& operator=(Port&&) = delete;

    /// Post an event to this port, to be handled in its active object's thread.
    ///
    /// @param event_id the event identifier, defined by the owning module
    /// @param opt_data optional data, commonly a pointer to an object
    /// @return true if the event was queued
    bool post(uint32_t event_id, uint32_t opt_data = 0U);

    /// Post an event to this port from an ISR.
    ///
    /// @param event_id the event identifier, defined by the owning module
    /// @param opt_data optional data, commonly a pointer to an object
    /// @return true if the event was queued
    bool post_from_isr(uint32_t event_id, uint32_t opt_data = 0U);

    /// Get the identifier of this port
    ///
    /// @return the port identifier
    app::PortList get_id() const;

    /// Handle an event addressed to this port.
    ///
    /// Called from the owning active object's thread. Implemented by each module.
    ///
    /// @param event_id the event identifier
    /// @param opt_data optional data carried by the event
    virtual void handle_event(uint32_t event_id, uint32_t opt_data) = 0;

private:
    /// The active object whose thread runs this port's handler
    ActiveObject& m_active_object;

    /// The identifier of this port
    app::PortList m_port_id;
};

} // namespace eda

/// @}
