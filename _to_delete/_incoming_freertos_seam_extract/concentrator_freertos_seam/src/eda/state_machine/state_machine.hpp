/// @addtogroup grp_eda
/// @{
///
/// @file state_machine.hpp
///
/// Header file that declares the state machine class.

#pragma once

namespace eda
{
class StateMachine;
};

#include <cstdint>

namespace eda
{

/// A StateMachine manages the execution flow between multiple State objects.
/// The StateMachine controls which State is active at a given time and
/// ensures transitions occur according to the defined logic.
///
/// Note: A meaningful StateMachine requires at least two States, otherwise
/// transitions would never occur.
class State
{
    friend StateMachine;

public:
    /// Constructor sets the state name used for logging.
    ///
    /// @param name A short string used to name the state.
    /// @param state_machine Pointer to the StateMachine that this state belongs to.
    State(const char* name, StateMachine* state_machine) : m_state_machine(state_machine), m_name(name)
    {
    }

    /// The Entry is optionally defined by a derived state.
    /// It is the action performed when the state is entered.
    virtual void entry();

    /// The Exit is optionally defined by a derived state.
    /// It is the action performed when the state is exited.
    virtual void exit();

    /// A derived class's dispatch_event should typically consist of a switch statement
    /// for handling each event type within the state.
    ///
    /// @param event_id The identifier of the event to be dispatched.
    /// @param opt_data_address Optional data associated with the event.
    virtual void dispatch_event(uint32_t event_id, uint32_t opt_data_address) = 0;

protected:
    /// Pointer to the StateMachine that this state belongs to.
    StateMachine* m_state_machine;

private:
    /// Name of the state used for logging.
    const char* m_name;
};

/// StateMachine consists of an aggregation of States. The StateMachine
/// tracks the currently active State and performs state changes when requested.
/// A derived StateMachine class should define its States and the initial State.
class StateMachine
{
public:
    /// Constructor
    ///
    /// @param name A short string used to name the state machine in logging.
    /// @param initial_state The initial State that the StateMachine should start in.
    StateMachine(const char* name, State* initial_state)
        : m_current_state(initial_state), m_previous_state(nullptr), m_next_state(nullptr),
          m_name(name)
    {
    }

    /// Perform the initial transition.
    void init();

    /// The InitAction is optionally defined by a derived state.
    /// It is the action performed when the state machine is initialized prior to entry to the first
    /// state. If the initial state should be changed, this is the place to do so via calling
    /// set_next_state.
    virtual void init_action();

    /// This routes an event to the current State's dispatcher.
    ///
    /// @param event_id The identifier of the event to be dispatched.
    /// @param opt_data_address Optional data associated with the event.
    void dispatch_event(uint32_t event_id, uint32_t opt_data_address);

    /// Transition from one State to another, performing exit and entry actions.
    ///
    /// @param new_state The new State to transition to.
    void change_state(State* new_state);

    /// Gets the value of the last state from history.
    ///
    /// @return Pointer to last state.
    State* get_last_state();

    /// Transition to the last state from history.
    /// The last state is the previous state before the current one.
    void return_to_last_state();

    /// Transition to the next state if set.
    void change_to_next_state();

    /// Set next state. This enables storing of conditional logic for state transitions
    /// if the associated event is not a transitional event.
    ///
    /// @param new_state Pointer to the next state to transition to.
    void set_next_state(State* new_state);

    /// Check if we have a next state set. This way we know whether we can call
    /// change_to_next_state().
    ///
    /// @return Boolean, true if next state is set.
    bool next_state_is_set();

    /// Get name of current state.
    ///
    /// @return Pointer to string containing name of current state.
    const char* get_current_state_name() const;

private:
    /// Pointer to the current State.
    State* m_current_state;

    /// Pointer to the previous State.
    State* m_previous_state;

    /// Pointer to the next State to transition to.
    State* m_next_state;

    /// Name of the state machine used for logging.
    const char* m_name;
};
} // namespace eda

/// @}
