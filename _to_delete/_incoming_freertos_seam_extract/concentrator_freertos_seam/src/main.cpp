/// @file main.cpp
///
/// The entry point of the firmware.
///
/// Everything of substance happens in app::initialize() and then in the threads
/// it creates. This file exists to call it and to fail loudly if it did not
/// work.

#include "app/app.hpp"
#include "app/port.hpp"
#include "app/port_list.hpp"

#include "eda/port/port.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_APP_LOG_LEVEL);

int main()
{
    if (!app::initialize())
    {
        LOG_ERR("initialization failed");

        eda::Port::send_event(app::PortList::APP_PORT,
                              static_cast<uint32_t>(app::Event::SERVICES_FAILED), 0);
    }
    else
    {
        eda::Port::send_event(app::PortList::APP_PORT,
                              static_cast<uint32_t>(app::Event::SERVICES_READY), 0);
    }

    // Every active object runs on its own thread from here. The main thread has
    // nothing left to do, and returning from main() is the idiomatic way to say
    // so on Zephyr.
    return 0;
}
