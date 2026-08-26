/// @defgroup grp_assert Assert
/// @brief A halt-on-impossible-condition macro
///
/// @addtogroup grp_assert
/// @{
///
/// @file assert.hpp
///
/// Header file that defines an assert macro.

#pragma once

// Ported from `deepsight-polaris-software`'s src/assert/assert.hpp, with one
// change: the reference gates on BUILD_DEBUG, a symbol its build system
// defines. Zephyr already has CONFIG_DEBUG for the same idea, so both are
// accepted and neither repository has to change its build to share this file.
#if defined(BUILD_DEBUG) || defined(CONFIG_DEBUG)

/// Halt if a condition that must never be false is false.
///
/// The spin is deliberate. A device stopped inside the failing function still
/// has the stack that led there, which is what a debugger needs; returning or
/// resetting would erase it. On a build with the watchdog running, the spin
/// stops feeding it and the device resets a few seconds later, so an assert
/// that fires in the field is a reboot with a watchdog reset reason rather than
/// a unit that hangs forever.
///
/// Compiled out entirely outside a debug build, so nothing that must happen may
/// happen *inside* the condition.
#define ASSERT_CRITICAL(condition) \
    do                             \
    {                              \
        if (!((condition)))        \
        {                          \
            while (1)              \
            {}                     \
        }                          \
    } while (0)

#else

#define ASSERT_CRITICAL(condition) ((void)0)

#endif

/// @}
