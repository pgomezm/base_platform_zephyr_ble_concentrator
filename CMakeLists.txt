cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(base_platform_zephyr_ble_concentrator LANGUAGES C CXX)

# Modules include each other as "hal/ble/ble.hpp", "svc/comms/subsystem.hpp"
# and so on, so every include path is readable as a layer plus a module.
target_include_directories(app PRIVATE src)

# Sources are listed rather than globbed: a file that is added but not built is
# a mistake worth catching at review time, not at link time.
target_sources(app PRIVATE
    src/main.cpp

    # Application
    src/app/app.cpp
    src/app/port.cpp
    src/app/state_machine/state_machine.cpp
    src/app/state_machine/startup/startup.cpp
    src/app/state_machine/listening/listening.cpp
    src/app/state_machine/dispatching/dispatching.cpp
    src/app/state_machine/soft_error/soft_error.cpp
    src/app/state_machine/hard_error/hard_error.cpp

    # Event driven architecture
    src/eda/active_object/active_object.cpp
    src/eda/port/port.cpp
    src/eda/state_machine/state_machine.cpp
    src/eda/timer/timer.cpp

    # Hardware abstraction layer
    src/hal/ble/zephyr/ble_zephyr.cpp
    src/hal/lora/zephyr/lora_zephyr.cpp
    src/hal/system/zephyr/system_zephyr.cpp
    src/hal/led/zephyr/led_zephyr.cpp
    src/hal/watchdog/zephyr/watchdog_zephyr.cpp

    # Services
    src/svc/acquisition/port.cpp
    src/svc/acquisition/subsystem.cpp
    src/svc/device_table/subsystem.cpp
    src/svc/comms/port.cpp
    src/svc/comms/subsystem.cpp
    src/svc/system_diagnostics/port.cpp
    src/svc/system_diagnostics/subsystem.cpp
)
