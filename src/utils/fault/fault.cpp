/// @addtogroup grp_utils_fault
/// @{
///
/// @file fault.cpp
///
/// Source file that implements the fault latch.

#include "utils/fault/fault.hpp"

namespace utils::fault
{
namespace
{

/// Set once and never cleared. Only a reset gets the device out of a fault.
volatile bool s_active = false;

/// The first reason reported.
volatile Reason s_reason = Reason::NONE;

} // namespace

void report(Reason reason)
{
    if (!s_active)
    {
        s_reason = reason;
        s_active = true;
    }
}

bool is_active()
{
    return s_active;
}

Reason get_reason()
{
    return s_reason;
}

const char* describe(Reason reason)
{
    switch (reason)
    {
    case Reason::EVENT_LOST:
        return "a one-shot event was lost to a full queue";

    case Reason::PORT_NOT_READY:
        return "a one-shot event went to an unregistered port";

    case Reason::UNRECOVERABLE:
        return "the application reached its unrecoverable error state";

    case Reason::NONE:
    default:
        return "none";
    }
}

} // namespace utils::fault

/// @}
