/// @addtogroup grp_utils
/// @{
///
/// @file log.hpp
///
/// Header file that declares the logging interface.

#pragma once

#include <zephyr/logging/log.h>

/// Thin wrapper over Zephyr's logging macros.
///
/// Every module logs through these names rather than Zephyr's directly, so the
/// backend can be swapped (or compiled out per module) without touching call
/// sites. `LOG_MODULE_DECLARE`/`LOG_MODULE_REGISTER` are still used directly in
/// each translation unit, because they must expand at file scope.

#define LOG_MODULE_ERR(...) LOG_ERR(__VA_ARGS__)
#define LOG_MODULE_WARN(...) LOG_WRN(__VA_ARGS__)
#define LOG_MODULE_INF(...) LOG_INF(__VA_ARGS__)
#define LOG_MODULE_DBG(...) LOG_DBG(__VA_ARGS__)

/// @}
