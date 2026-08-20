/// @defgroup grp_app Application
///
/// The high-level logic of the concentrator.
///
/// @addtogroup grp_app
/// @{
///
/// @file app.hpp
///
/// Header file that declares the App interface.

#pragma once

#include "port.hpp"
#include "state_machine/state_machine.hpp"

#include "eda/active_object/active_object.hpp"

namespace app
{

/// Application class.
///
/// This class provides the highest-level logic of the application.
class App
{
public:
    /// Get the singleton instance of the `App`.
    ///
    /// @return The singleton instance of the application `App`.
    static App& get_instance();

    /// Initialize the application.
    ///
    /// Brings up the HAL, then the services, then the application state
    /// machine, in that order, and posts SERVICES_READY or SERVICES_FAILED to
    /// its own port when it is done. Nothing above this decides what a failure
    /// means: the state machine does.
    void initialize();

    /// Run the application main loop.
    ///
    /// Every active object runs on its own thread, created during initialize().
    /// On Zephyr the scheduler is already running by the time main() is
    /// entered, so this returns immediately and the calling thread ends. It
    /// exists to keep the entry point identical in shape to the FreeRTOS
    /// firmware this project follows, where the equivalent call is what starts
    /// the scheduler.
    void run();

    /// Get the application state machine instance.
    ///
    /// @return The application state machine instance.
    StateMachine& get_state_machine();

    /// Get the application port instance.
    ///
    /// @return The application port instance.
    Port& get_port();

private:
    // The constructor is private since this is a singleton
    App();

    // Delete copy and move constructors and assignment operators, since
    // singletons shouldn't be copied/moved
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;
    ~App() = default;

    eda::ActiveObject m_active_object;

    Port m_port;
};

} // namespace app

/// @}
