/// @addtogroup grp_app
/// @{
///
/// @file listening.hpp
///
/// Header file that declares the LISTENING state: scanning and collecting readings.

#pragma once

#include "eda/state_machine/state_machine.hpp"

#include <cstdint>

namespace app
{

/// Scanning and collecting readings. The concentrator's idle state.
class ListeningState : public eda::State
{
public:
    /// Constructor
    ///
    /// @param state_machine the state machine this state belongs to
    explicit ListeningState(eda::StateMachine& state_machine);

    /// Called when the state is entered.
    void entry() override;

    /// Called when the state is left.
    void exit() override;

    /// Handle an event while this state is current.
    ///
    /// @param event_id the event identifier, an app::Event value
    /// @param opt_data_address optional data carried by the event
    void dispatch_event(uint32_t event_id, uint32_t opt_data_address) override;
};

} // namespace app

/// @}
