/// @addtogroup grp_eda
/// @{
///
/// @file state_machine.hpp
///
/// Header file that declares the base state and state machine.

#pragma once

#include <cstdint>

namespace eda
{

/// Base class for a single state of a state machine.
///
/// States are statically allocated by the module that owns them; the state
/// machine only ever holds a pointer to one.
class State
{
public:
    /// Virtual destructor
    virtual ~State() = default;

    /// Called once when this state becomes the current state.
    virtual void on_entry() {}

    /// Called once when this state stops being the current state.
    virtual void on_exit() {}

    /// Handle an event while this state is current.
    ///
    /// @param event_id the event identifier
    /// @param opt_data optional data carried by the event
    virtual void handle_event(uint32_t event_id, uint32_t opt_data) = 0;

    /// Human-readable name, for logging and for the status word.
    ///
    /// @return the state name
    virtual const char* get_name() const = 0;
};

/// A flat state machine: one current state, explicit transitions.
///
/// Deliberately flat rather than hierarchical, matching the shape of the
/// diagrams in docs/Concentrator_StatusDiagram.drawio.
class StateMachine
{
public:
    /// Constructor
    StateMachine();

    /// Set the initial state and run its entry action.
    ///
    /// @param initial_state the state to start in
    void init(State& initial_state);

    /// Leave the current state and enter another one.
    ///
    /// Runs the current state's exit action, then the new state's entry action.
    /// A transition to the state already current is a no-op, so a state can
    /// safely request it without re-running its own entry action.
    ///
    /// @param next_state the state to transition into
    void transition_to(State& next_state);

    /// Forward an event to the current state.
    ///
    /// @param event_id the event identifier
    /// @param opt_data optional data carried by the event
    void dispatch(uint32_t event_id, uint32_t opt_data);

    /// Get the current state.
    ///
    /// @return pointer to the current state, or nullptr before init()
    State* get_current_state() const;

private:
    /// The state currently active
    State* m_p_current_state;
};

} // namespace eda

/// @}
