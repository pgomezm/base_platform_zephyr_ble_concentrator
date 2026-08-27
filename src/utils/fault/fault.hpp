/// @defgroup grp_utils_fault Fault
/// @ingroup grp_utils
/// @brief The latch that says this device has stopped being trustworthy
///
/// @addtogroup grp_utils_fault
/// @{
///
/// @file fault.hpp
///
/// Header file that declares the fault latch.

#pragma once

#include <cstdint>

namespace utils::fault
{

/// Why the device gave up.
enum class Reason : uint8_t
{
    /// No fault. The normal state.
    NONE,

    /// A one-shot event never reached its queue.
    EVENT_LOST,

    /// A one-shot event was addressed to a port nobody registered.
    PORT_NOT_READY,

    /// The application reached its unrecoverable error state.
    UNRECOVERABLE,
};

/// Latch a fault.
///
/// Sets a flag and remembers the reason. That is all it does, so it is safe to
/// call from an interrupt, and safe to call when the thing that failed is the
/// event system itself.
///
/// The first reason wins. A fault usually knocks over whatever runs next, and
/// the interesting one is the first.
///
/// @param reason what went wrong
void report(Reason reason);

/// @return true once report() has been called
bool is_active();

/// @return the latched reason, NONE if there is no fault
Reason get_reason();

/// @param reason the reason to name
/// @return a short text for the log
const char* describe(Reason reason);

} // namespace utils::fault

/// @}
