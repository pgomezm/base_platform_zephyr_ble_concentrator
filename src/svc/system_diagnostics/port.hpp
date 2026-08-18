/// @addtogroup grp_svc_system_diagnostics
/// @{
///
/// @file port.hpp
///
/// Header file that declares the port of the system diagnostics service.

#pragma once

#include "eda/port/port.hpp"

#include <cstdint>

namespace svc::system_diagnostics
{

/// Events handled by the system diagnostics service.
enum class Event : uint32_t
{
    /// Invalid, never posted.
    INVALID,

    /// The heartbeat period elapsed: feed the watchdog and check health.
    HEARTBEAT_DUE,
};

/// The port of the system diagnostics service.
class Port : public eda::Port
{
public:
    /// Constructor
    ///
    /// @param active_object the active object whose thread handles these events
    explicit Port(eda::ActiveObject& active_object);

    /// Handle an event addressed to this service.
    ///
    /// @param event_id the event identifier, an Event value
    /// @param opt_data optional data carried by the event
    void handle_event(uint32_t event_id, uint32_t opt_data) override;
};

} // namespace svc::system_diagnostics

/// @}
