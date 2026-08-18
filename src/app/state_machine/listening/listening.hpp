/// @addtogroup grp_app
/// @{
///
/// @file listening.hpp
///
/// Header file that declares the Scanning and collecting readings. The concentrator's idle state. state.

#pragma once

#include "eda/state_machine/state_machine.hpp"

#include <cstdint>

namespace app
{

/// Scanning and collecting readings. The concentrator's idle state.
class ListeningState : public eda::State
{
public:
    /// Called when the state is entered.
    void on_entry() override;

    /// Called when the state is left.
    void on_exit() override;

    /// Handle an event while this state is current.
    ///
    /// @param event_id the event identifier, an app::Event value
    /// @param opt_data optional data carried by the event
    void handle_event(uint32_t event_id, uint32_t opt_data) override;

    /// @return the name of this state
    const char* get_name() const override;
};

} // namespace app

/// @}
