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

    /// Join the LoRaWAN network.
    JOIN_NETWORK,

    /// The dispatch period elapsed: build and send an uplink.
    DISPATCH_DUE,

    /// Send an uplink now, without waiting for the period.
    DISPATCH_NOW,
};

/// The port of the comms service.
class Port : public eda::Port
{
public:
    /// Constructor
    ///
    /// @param active_object the active object whose thread handles these events
    explicit Port(eda::ActiveObject& active_object);

    /// Handle an event addressed to this service.
    ///
    /// Runs in the comms thread.
    ///
    /// @param event_id the event identifier, an Event value
    /// @param opt_data optional data carried by the event
    void handle_event(uint32_t event_id, uint32_t opt_data) override;
};

} // namespace svc::comms

/// @}
