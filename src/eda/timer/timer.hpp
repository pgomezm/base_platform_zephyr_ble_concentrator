/// @addtogroup grp_eda
/// @{
///
/// @file timer.hpp
///
/// Header file that declares the event timer.

#pragma once

#include <cstdint>

#include <zephyr/kernel.h>

namespace eda
{
class Port; // Forward declaration

/// A timer that posts an event to a port when it expires.
///
/// The expiry runs in Zephyr's system timer ISR context, so it does nothing but
/// post an event: all real work happens later in the target port's thread.
class Timer
{
public:
    /// Constructor
    ///
    /// @param port the port that receives the event on expiry
    /// @param event_id the event identifier to post on expiry
    Timer(Port& port, uint32_t event_id);

    // Held by address from the kernel timer's user data, so no copies or moves.
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    /// Start a one-shot timer.
    ///
    /// @param duration_ms time until expiry, in milliseconds
    void start_once(uint32_t duration_ms);

    /// Start a periodic timer.
    ///
    /// @param period_ms period between expiries, in milliseconds
    void start_periodic(uint32_t period_ms);

    /// Stop the timer. Safe to call when it is not running.
    void stop();

    /// Check whether the timer is currently running.
    ///
    /// @return true if the timer has time remaining
    bool is_running() const;

private:
    /// Kernel expiry callback. Runs in ISR context: posts and returns.
    ///
    /// @param p_timer the kernel timer that expired
    static void expiry_handler(struct k_timer* p_timer);

    /// The kernel timer
    struct k_timer m_timer;

    /// The port that receives the event on expiry
    Port& m_port;

    /// The event identifier posted on expiry
    uint32_t m_event_id;
};

} // namespace eda

/// @}
