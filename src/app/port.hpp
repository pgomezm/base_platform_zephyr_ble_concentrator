/// @addtogroup grp_app
/// @{
///
/// @file port.hpp
///
/// Header file that declares the port of the application.

#pragma once

#include "eda/port/port.hpp"

#include <cstdint>

namespace app
{

/// Events handled by the application state machine.
enum class Event : uint32_t
{
    /// Invalid, never posted.
    INVALID,

    /// Every service initialized successfully.
    SERVICES_READY,

    /// A service failed to initialize.
    SERVICES_FAILED,

    /// The LoRaWAN join succeeded.
    NETWORK_JOINED,

    /// The LoRaWAN join failed or timed out.
    NETWORK_JOIN_FAILED,

    /// An uplink dispatch started.
    DISPATCH_STARTED,

    /// An uplink dispatch finished, successfully or not.
    DISPATCH_FINISHED,

    /// A recoverable failure occurred. The device retries after a delay.
    SOFT_ERROR,

    /// An unrecoverable failure occurred. The device stops operating.
    HARD_ERROR,

    /// The retry delay elapsed.
    RETRY_TIMEOUT,
};

/// The port of the application.
class Port : public eda::Port
{
public:
    /// Constructor
    ///
    /// @param active_object the active object whose thread handles these events
    explicit Port(eda::ActiveObject& active_object);

    /// Handle an event addressed to the application.
    ///
    /// Forwards it to the application state machine, which is the only thing
    /// that decides what an event means.
    ///
    /// @param event_id the event identifier, an Event value
    /// @param opt_data optional data carried by the event
    void handle_event(uint32_t event_id, uint32_t opt_data) override;
};

} // namespace app

/// @}
