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

/// How long to wait for the LoRa join to succeed before declaring a soft error,
/// in milliseconds.
constexpr uint32_t k_join_timeout_ms = 60U * 1000U;

/// How long to wait before retrying after a soft error, in milliseconds.
constexpr uint32_t k_soft_error_retry_ms = 30U * 1000U;

/// Maximum number of consecutive soft errors before the device gives up and
/// moves to the hard error state.
constexpr uint8_t k_max_consecutive_soft_errors = 5U;

} // namespace app

/// @}
