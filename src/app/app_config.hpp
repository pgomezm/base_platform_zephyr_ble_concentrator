/// @addtogroup grp_app
/// @{
///
/// @file app_config.hpp
///
/// Header file that declares configuration options for the `app` module.

#pragma once

#include "config.hpp"

#include <cstdint>

namespace app
{

/// How long to wait for the uplink transport to connect before declaring a soft
/// error,
/// in milliseconds.
constexpr uint32_t JOIN_TIMEOUT_MS = 60U * 1000U;

/// How long to wait before retrying after a soft error, in milliseconds.
constexpr uint32_t SOFT_ERROR_RETRY_MS = 30U * 1000U;

/// Maximum number of consecutive soft errors before the device gives up and
/// moves to the hard error state.
constexpr uint8_t MAX_CONSECUTIVE_SOFT_ERRORS = 5U;

} // namespace app

/// @}
