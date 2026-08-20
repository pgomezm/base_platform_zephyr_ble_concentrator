/// @file main.cpp
///
/// The entry point of the firmware.
///
/// Everything of substance happens in app::App. This file exists to hand
/// control to it and nothing else: no bring-up order, no events, no decisions.

#include "app/app.hpp"

int main()
{
    app::App::get_instance().initialize();
    app::App::get_instance().run();

    return 0;
}
