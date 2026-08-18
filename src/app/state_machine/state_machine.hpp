/// @addtogroup grp_app
/// @{
///
/// @file state_machine.hpp
///
/// Header file that declares the application state machine.

#pragma once

#include "eda/state_machine/state_machine.hpp"

#include <cstdint>

namespace app
{

/// The lifecycle of the device.
///
/// These are the states from docs/Concentrator_StatusDiagram.drawio, in the
/// vocabulary of the 5K-IoT status word. LISTENING is the concentrator's IDLE:
/// the radio is scanning and nothing else is in flight. DISPATCHING is its
/// BUSY: an uplink is being built and sent.
enum class StateId : uint8_t
{
    STARTUP,
    LISTENING,
    DISPATCHING,
    SOFT_ERROR,
    HARD_ERROR,
};

/// The application state machine.
///
/// Owns every state object and the transitions between them. States never
/// transition each other directly: a state asks this class, which keeps the
/// full transition table readable in one file.
class StateMachine
{
public:
    /// Constructor
    StateMachine();

    /// Enter the initial state.
    void initialize();

    /// Dispatch an event to the current state.
    ///
    /// @param event_id the event identifier, an app::Event value
    /// @param opt_data optional data carried by the event
    void dispatch(uint32_t event_id, uint32_t opt_data);

    /// Transition to a state.
    ///
    /// @param state_id which state to enter
    void transition_to(StateId state_id);

    /// Identifier of the current state.
    ///
    /// This is what a status report would serialize: the concentrator's own
    /// device state, straight out of the state machine rather than tracked
    /// alongside it.
    ///
    /// @return the current state identifier
    StateId get_current_state_id() const;

private:
    /// The underlying flat state machine.
    eda::StateMachine m_state_machine;

    /// The identifier of the current state.
    StateId m_current_state_id;
};

/// Get the application state machine.
///
/// @return reference to the single instance
StateMachine& get_state_machine();

} // namespace app

/// @}
