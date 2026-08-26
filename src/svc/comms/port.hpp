/// @addtogroup grp_svc_comms
/// @{
///
/// @file port.hpp
///
/// Header file that declares the port of the comms service.

#pragma once

#include "eda/port/port.hpp"

#include <cstdint>

namespace svc::comms
{

/// Events handled by the comms service.
enum class Event : uint32_t
{
    /// Invalid, never posted.
    INVALID,

    /// Connect the uplink transport to its network.
    JOIN_NETWORK,

    /// The dispatch period elapsed: build and send an uplink.
    DISPATCH_DUE,

    /// Send an uplink now, without waiting for the period.
    DISPATCH_NOW,

    /// Begin dispatching: start the periodic timer and allow transmitting.
    START_DISPATCH,

    /// Stop dispatching: stop the periodic timer and forbid transmitting.
    ///
    /// Stopping the timer is not on its own enough. A timer that fired just
    /// before this event was posted has already put a DISPATCH_DUE in this
    /// port's queue, ahead of this one, and stopping the timer does not take it
    /// back out. The flag this event clears is what makes that queued event a
    /// no-op instead of one more uplink from a device that was told to go
    /// quiet.
    STOP_DISPATCH,
};

/// The port of the comms service.
class Port : public eda::Port
{
public:
    /// Constructor. Does not join the port registry: call init() before
    /// anything sends an event to app::PortList::COMMS_PORT.
    Port() = default;

    /// Handle an event addressed to this service.
    ///
    /// Runs in the comms thread.
    ///
    /// @param event_id the event identifier, an Event value
    /// @param opt_data_address optional data carried by the event
    void execute_event(uint32_t event_id, uint32_t opt_data_address) override;
};

} // namespace svc::comms

/// @}
