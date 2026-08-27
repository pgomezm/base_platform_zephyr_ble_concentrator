/// @defgroup grp_utils_log Logging
/// @ingroup grp_utils
///
/// The logging seam.
///
/// @addtogroup grp_utils_log
/// @{
///
/// @file log.hpp
///
/// Header file that declares the logging interface.
///
/// Every module logs through these names and no module includes a logging
/// header of the RTOS or the vendor SDK. That is the same reason `hal::os`
/// exists: the firmware is meant to survive a change of platform, and a call
/// site that names a logging header of the RTOS or the vendor SDK pins the
/// file to that platform just as firmly as one that calls into its kernel.

#pragma once

#if defined(__ZEPHYR__)

#include <zephyr/logging/log.h>

/// Declare the log module this translation unit owns.
///
/// Exactly one file per module. The level comes from `CONFIG_APP_LOG_LEVEL`, so
/// call sites do not name a Kconfig symbol either. Per-module levels are a
/// Zephyr feature this seam does not expose yet; see log.md.
///
/// @param name the module name, unquoted, as it appears in the log output
#define LOG_MODULE_DEFINE(name) LOG_MODULE_REGISTER(name, CONFIG_APP_LOG_LEVEL)

/// Log into a module some other translation unit owns.
///
/// The state machine's states are the case this exists for: six files that all
/// log as `app` so their output reads as one story rather than six.
///
/// @param name the module name, unquoted, matching a LOG_MODULE_DEFINE
#define LOG_MODULE_USE(name) LOG_MODULE_DECLARE(name, CONFIG_APP_LOG_LEVEL)

/// Something failed and the firmware could not carry out what was asked.
#define LOG_ERROR(...) LOG_ERR(__VA_ARGS__)

/// Something is wrong but the firmware carried on.
#define LOG_WARNING(...) LOG_WRN(__VA_ARGS__)

/// Normal operation worth recording: bring-up, joins, dispatches.
#define LOG_INFO(...) LOG_INF(__VA_ARGS__)

/// Detail useful while debugging and noise otherwise.
#define LOG_DEBUG(...) LOG_DBG(__VA_ARGS__)

#else // !__ZEPHYR__

// The fallback exists so that the claim this file makes is testable: a
// translation unit that logs only through these names compiles off Zephyr.
// Anything wired to a real output on another platform replaces this block, and
// the call sites do not change. Deliberately printf and not a queue: this path
// is for a host build or a bring-up on bare metal, neither of which is where
// logging performance is decided.

#include <cstdio>

#define LOG_MODULE_DEFINE(name) static const char* const s_log_module_name = #name
#define LOG_MODULE_USE(name) static const char* const s_log_module_name = #name

#define LOG_ERROR(...)                                 \
    do                                                 \
    {                                                  \
        (void)printf("<err> %s: ", s_log_module_name); \
        (void)printf(__VA_ARGS__);                     \
        (void)printf("\n");                            \
    }                                                  \
    while (0)

#define LOG_WARNING(...)                               \
    do                                                 \
    {                                                  \
        (void)printf("<wrn> %s: ", s_log_module_name); \
        (void)printf(__VA_ARGS__);                     \
        (void)printf("\n");                            \
    }                                                  \
    while (0)

#define LOG_INFO(...)                                  \
    do                                                 \
    {                                                  \
        (void)printf("<inf> %s: ", s_log_module_name); \
        (void)printf(__VA_ARGS__);                     \
        (void)printf("\n");                            \
    }                                                  \
    while (0)

#define LOG_DEBUG(...)                                 \
    do                                                 \
    {                                                  \
        (void)printf("<dbg> %s: ", s_log_module_name); \
        (void)printf(__VA_ARGS__);                     \
        (void)printf("\n");                            \
    }                                                  \
    while (0)

#endif // __ZEPHYR__

/// @}
