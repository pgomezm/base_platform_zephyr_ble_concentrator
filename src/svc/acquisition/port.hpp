/// @addtogroup grp_svc_acquisition
/// @{
///
/// @file port.hpp
///
/// Header file that declares the port of the acquisition service.

#pragma once

#include "eda/port/port.hpp"

#include <cstdint>

namespace svc::acquisition
{

/// Events handled by the acquisition service.
enum class Event : uint32_t
{
    /// Invalid, never posted.
    INVALID,

    /// Start scanning for endpoint advertisements.
    START_SCAN,

    /// Stop scanning.
    STOP_SCAN,

    /// One or more advertising reports are waiting in the report pool.
    ///
    /// Posted from the BLE callback, which runs in the Bluetooth stack's own
    /// receive context. The handler drains the pool, so several reports may be
    /// consumed by a single occurrence of this event.
    ADV_REPORT_AVAILABLE,
};

/// The port of the acquisition service.
class Port : public eda::Port
{
public:
    /// Constructor. Does not join the port registry: call init() before
    /// anything sends an event to eda_config::PortList::ACQUISITION_PORT.
    Port() = default;

    /// Handle an event addressed to this service.
    ///
    /// Runs in the acquisition thread.
    ///
    /// @param event_id the event identifier, an Event value
    /// @param opt_data_address optional data carried by the event
    void execute_event(uint32_t event_id, uint32_t opt_data_address) override;
};

} // namespace svc::acquisition

/// @}
