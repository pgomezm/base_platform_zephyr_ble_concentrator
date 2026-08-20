cmake_minimum_required(VERSION 3.20.0)

# Snippets shipped with this application (see snippets/). SNIPPET_ROOT covers
# only ZEPHYR_BASE unless the application adds itself, and it has to be set
# before find_package(Zephyr) because that is what processes -S.
list(APPEND SNIPPET_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(base_platform_zephyr_ble_concentrator LANGUAGES C CXX)

# Modules include each other as "hal/ble/ble.hpp", "svc/comms/subsystem.hpp"
# and so on, so every include path is readable as a layer plus a module.
target_include_directories(app PRIVATE src)

# Function-local statics (every get_instance() in this firmware) make GCC emit
# calls to __cxa_guard_acquire/__cxa_guard_release, the thread-safe-init guards.
# Those live in libstdc++, which this build deliberately does not link
# (CONFIG_REQUIRES_FULL_LIBCPP=n in prj.conf), so the link fails on them.
#
# Dropping the guards is safe here because it is not left to chance:
# app::App::initialize() constructs every HAL singleton explicitly, before it
# creates the first thread. Read that function before adding a new singleton.
target_compile_options(app PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>)

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
    src/eda/idle_hook/idle_hook.cpp
    src/eda/port/port.cpp
    src/eda/state_machine/state_machine.cpp
    src/eda/timer/timer.cpp

    # Hardware abstraction layer
    src/hal/ble/zephyr/ble_zephyr.cpp
    src/hal/gpio/zephyr/gpio_zephyr.cpp
    src/hal/led/led.cpp
    src/hal/os/zephyr/os_zephyr.cpp
    src/hal/system/zephyr/system_zephyr.cpp
    src/hal/watchdog/zephyr/watchdog_zephyr.cpp

    # Services
    src/svc/acquisition/subsystem.cpp
    src/svc/device_table/subsystem.cpp
    src/svc/comms/subsystem.cpp
    src/svc/system_diagnostics/subsystem.cpp
)

# Exactly one link backend is compiled, chosen by the "Uplink transport" menu
# in Kconfig. This is what makes one repository build both concentrator
# variants instead of forking: everything above src/hal/link is identical.
if(CONFIG_APP_LINK_LORA)
    target_sources(app PRIVATE src/hal/link/lora/link_lora.cpp)
elseif(CONFIG_APP_LINK_TCP)
    target_sources(app PRIVATE src/hal/link/tcp/link_tcp.cpp)
else()
    message(FATAL_ERROR "No link backend selected. Set CONFIG_APP_LINK_LORA or CONFIG_APP_LINK_TCP.")
endif()
