/// @addtogroup grp_app
/// @{
///
/// @file tasks_priorities.hpp
///
/// Header file that declares the priority of each thread in the application.

#pragma once

namespace app
{

/// Thread priorities, one per active object.
///
/// Zephyr convention: lower number means higher priority, and every value here
/// is a *preemptible* priority (>= 0). Zephyr's own Bluetooth threads run at
/// cooperative priorities (< 0), so every thread declared here sits below the
/// BLE stack by construction. That is the first rule in
/// docs/ARCHITECTURE.md section 4: nothing of ours may delay the radio.
///
/// The order among our own threads:
///   acquisition > comms > app > system_diagnostics
///
/// `acquisition` is highest because it drains the advertising reports the BLE
/// callback enqueues; if it falls behind, reports are dropped. `comms` is next
/// so a dispatch is not starved by application logic. `system_diagnostics` is
/// last because nothing waits on it.
enum class TaskPriorities : int
{
    /// BLE scan report parsing. Highest of ours, drains the report pool.
    ACQUISITION = 3,

    /// Uplink dispatch: build, fragment, send over LoRa.
    COMMS = 4,

    /// Application state machine.
    APP = 5,

    /// Heartbeat and health checks. Nothing waits on it.
    SYSTEM_DIAGNOSTICS = 7,
};

} // namespace app

/// @}
