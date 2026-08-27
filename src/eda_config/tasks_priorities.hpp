/// @addtogroup grp_eda_config
/// @{
///
/// @file tasks_priorities.hpp
///
/// Header file that declares the priority of each thread this project runs.

#pragma once

namespace eda_config
{

/// Thread priorities, one per active object.
///
/// The values are the project's; the header path and the type name are eda::'s.
/// See eda_config.md.
///
/// Lower number means higher priority, and every value here is preemptible.
/// The radio stack's own threads run above all of these, so every thread
/// declared here sits below it by construction. That is the first rule in
/// docs/ARCHITECTURE.md section 4: nothing of ours may delay the radio.
///
/// The mapping from these numbers onto a backend's own priority scheme is
/// hal::os's problem, not this file's.
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

    /// Uplink dispatch: build, fragment, hand to the link.
    COMMS = 4,

    /// Application state machine.
    APP = 5,

    /// Heartbeat and health checks. Nothing waits on it.
    SYSTEM_DIAGNOSTICS = 7,
};

} // namespace eda_config

/// @}
