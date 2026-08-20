/// @addtogroup grp_app
/// @{
///
/// @file app.cpp
///
/// Source file that implements the App.

#include "app.hpp"
#include "port_list.hpp"
#include "tasks_priorities.hpp"

#include "eda/idle_hook/idle_hook.hpp"
#include "eda/port/port.hpp"
#include "hal/ble/ble.hpp"
#include "hal/gpio/gpio.hpp"
#include "hal/led/led.hpp"
#include "hal/lora/lora.hpp"
#include "hal/os/os.hpp"
#include "hal/watchdog/watchdog.hpp"
#include "svc/acquisition/subsystem.hpp"
#include "svc/comms/subsystem.hpp"
#include "svc/device_table/subsystem.hpp"
#include "svc/system_diagnostics/subsystem.hpp"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app, CONFIG_APP_LOG_LEVEL);

namespace app
{

App::App() = default;

App& App::get_instance()
{
    static App instance;

    return instance;
}

void App::initialize()
{
    LOG_INF("base_platform_zephyr_ble_concentrator starting");

    // Wire the idle hook once, here, since app owns overall bring-up order.
    // No callback is registered by default: see eda::IdleHook and
    // hal/os/os.md for why this exists with nothing using it yet.
    hal::os::register_idle_callback(&eda::IdleHook::invoke);

    // Construct every HAL singleton now, while this is still the only thread.
    //
    // The build compiles with -fno-threadsafe-statics (see CMakeLists.txt for
    // why), so a function-local static that two threads reach at once would be
    // a race. Forcing construction here, before init_task() creates the first
    // thread, is what makes that flag safe rather than merely cheap. A new
    // singleton anywhere in hal/ belongs in this list.
    (void)hal::gpio::ManagerFactory::get_instance();
    (void)hal::led::Manager::get_instance();
    (void)hal::ble::BleFactory::get_instance();
    (void)hal::lora::LoraFactory::get_instance();
    (void)hal::watchdog::WatchdogFactory::get_instance();

    // The application's own thread and port come up before any service, so a
    // service that fails during its own initialize() has somewhere to report to.
    m_active_object.init_task(TaskPriorities::APP, "app");
    m_port.init(PortList::APP_PORT, m_active_object);

    get_state_machine().init();

    bool services_ready = true;

    // The table before acquisition, since acquisition writes into it as soon as
    // the first advertisement arrives.
    svc::device_table::initialize();

    if (!svc::acquisition::initialize())
    {
        LOG_ERR("acquisition service failed to initialize");
        services_ready = false;
    }

    if (services_ready && !svc::comms::initialize())
    {
        LOG_ERR("comms service failed to initialize");
        services_ready = false;
    }

    if (services_ready && !svc::system_diagnostics::initialize())
    {
        LOG_ERR("system diagnostics failed to initialize");
        services_ready = false;
    }

    // The outcome of bring-up is an event like any other. main() does not
    // decide what a failed service means, and neither does this function: the
    // state machine does.
    const Event outcome = services_ready ? Event::SERVICES_READY : Event::SERVICES_FAILED;

    eda::Port::send_event(PortList::APP_PORT, static_cast<uint32_t>(outcome), 0U);
}

void App::run()
{
    // Nothing to do: see the note on App::run() in app.hpp.
}

Port& App::get_port()
{
    return m_port;
}

} // namespace app

/// @}
