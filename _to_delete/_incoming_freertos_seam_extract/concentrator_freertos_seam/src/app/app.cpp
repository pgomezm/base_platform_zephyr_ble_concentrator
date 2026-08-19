/// @addtogroup grp_app
/// @{
///
/// @file app.cpp
///
/// Source file that implements the application.

#include "app/app.hpp"
#include "app/port_list.hpp"
#include "app/state_machine/state_machine.hpp"

#include "eda/active_object/active_object.hpp"
#include "eda/idle_hook/idle_hook.hpp"
#include "hal/led/led.hpp"
#include "hal/os/os.hpp"
#include "svc/acquisition/subsystem.hpp"
#include "svc/comms/subsystem.hpp"
#include "svc/device_table/subsystem.hpp"
#include "svc/system_diagnostics/subsystem.hpp"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app, CONFIG_APP_LOG_LEVEL);

namespace app
{
namespace
{

/// The active object that runs the application.
eda::ActiveObject s_active_object;

/// The port of the application.
Port s_port;

} // namespace

bool initialize()
{
    LOG_INF("base_platform_zephyr_ble_concentrator starting");

    // Wire the idle hook once, here, since app owns overall bring-up order.
    // No callback is registered by default: see eda::IdleHook and
    // hal/os/os.md for why this exists with nothing using it yet.
    hal::os::register_idle_callback(&eda::IdleHook::invoke);

    // HAL first: services depend on it.
    if (!hal::led::initialize())
    {
        LOG_WRN("LEDs unavailable, continuing without them");
    }

    // The table before acquisition, since acquisition writes into it as soon as
    // the first advertisement arrives.
    svc::device_table::initialize();

    s_active_object.init_task(TaskPriorities::APP, "app");
    s_port.init(PortList::APP_PORT, s_active_object);

    if (!svc::acquisition::initialize())
    {
        LOG_ERR("acquisition service failed to initialize");
        return false;
    }

    if (!svc::comms::initialize())
    {
        LOG_ERR("comms service failed to initialize");
        return false;
    }

    if (!svc::system_diagnostics::initialize())
    {
        LOG_ERR("system diagnostics failed to initialize");
        return false;
    }

    get_state_machine().init();

    return true;
}

Port& get_port()
{
    return s_port;
}

} // namespace app

/// @}
