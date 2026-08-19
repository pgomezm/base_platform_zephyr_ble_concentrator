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
/// A thin subclass of eda::StateMachine: owns no state objects itself (those
/// are static globals in state_machine.cpp, exactly as
/// deepsight-altair-software's own state machines are expected to be built),
/// and adds only what an eda::StateMachine does not already provide — a
/// StateId badge for the current state, for the status word, plus
/// transition_to(StateId), the one readable place every valid transition is
/// listed.
class StateMachine : public eda::StateMachine
{
public:
    /// Constructor
    StateMachine();

    /// Set the initial state. Called once from init(), which app::app.cpp
    /// calls after every service is initialized.
    void init_action() override;

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
    /// The identifier of the current state.
    StateId m_current_state_id;
};

/// Get the application state machine.
///
/// @return reference to the single instance
StateMachine& get_state_machine();

} // namespace app

/// @}
