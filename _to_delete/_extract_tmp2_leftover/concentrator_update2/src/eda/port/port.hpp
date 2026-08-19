/// @addtogroup grp_eda
/// @{
///
/// @file port.hpp
///
/// Implementation of the Port class used for communication between Active Objects.

#pragma once

#include "app/port_list.hpp"
#include "eda/active_object/active_object.hpp"

#include <cstdint>

/// Maximum number of distinct event ids a single Port can register a callback
/// for. Ported from `deepsight-polaris-software`'s `eda::Port`, where this is a
/// preprocessor `#define` of the same name and value.
#define MAX_PORT_CALLBACKS 128

namespace eda
{
class ActiveObject; // forward declaration

using EventCallback = void (*)(uint32_t opt_data_address);

/// Error codes for subsystem responses
enum class SubsystemResultCode : uint32_t
{
    OK = 0,   ///< Operation completed successfully
    ERROR = 1 ///< Operation failed
};

/// Generic response structure for subsystem getter operations
/// Contains an error code and a pointer to the payload data
/// Used by subsystems to return both error status and data through port events
struct SubsystemResponse
{
    /// Error code: OK (0) = success, ERROR (1) = failure
    SubsystemResultCode error_code;

    /// Pointer to the payload data (nullptr if error occurred)
    /// The type and interpretation of payload depends on the specific getter
    uint32_t payload_address;

    SubsystemResponse() : error_code(SubsystemResultCode::OK), payload_address(0)
    {
    }

    SubsystemResponse(SubsystemResultCode error, uint32_t payload)
        : error_code(error), payload_address(payload)
    {
    }
};

/// A Port is how one module reaches another.
///
/// A module never calls another module's functions directly for anything
/// asynchronous: it addresses an event to that module's port id, and the
/// event is handled later in the owning active object's thread context. This
/// is what keeps the execution contexts in docs/ARCHITECTURE.md section 4
/// honest.
///
/// Ported from `deepsight-polaris-software`'s `eda::Port`: every port
/// registers itself into one static table by `app::PortList` id at `init()`
/// time, and `send_event()`/`send_event_from_isr()` look a port up by that id
/// rather than requiring the sender to hold a reference to it.
class Port
{
    friend class ActiveObject;

public:
    /// Constructor
    Port();

    /// Initialize the port with an ID and associate it with an active object
    ///
    /// @param port_id Port identifier from the PortList enum
    /// @param active_object Reference to the ActiveObject associated with this port
    void init(app::PortList port_id, ActiveObject& active_object);

    /// Set a callback function for a specific event ID
    ///
    /// @param event_id Event identifier
    /// @param callback Function to be called when the event is received
    void set_event_callback(uint32_t event_id, EventCallback callback);

    /// Send an event to a port
    ///
    /// @param port_id Target port identifier from the PortList enum
    /// @param event_id Event identifier
    /// @param opt_data_address Optional data associated with the event
    static void send_event(app::PortList port_id, uint32_t event_id, uint32_t opt_data_address);

    /// Send an event to a port from an ISR
    ///
    /// @param port_id Target port identifier from the PortList enum
    /// @param event_id Event identifier
    /// @param opt_data_address Optional data associated with the event
    static void send_event_from_isr(app::PortList port_id, uint32_t event_id,
                                    uint32_t opt_data_address);

    /// Port ID
    app::PortList m_port_id;

    /// Associated active object
    ActiveObject* m_active_object;

protected:
    /// Execute the callback
    ///
    /// @param event_id event identifier
    /// @param opt_data_address Optional data associated with the event
    void execute_callback(uint32_t event_id, uint32_t opt_data_address = 0);

private:
    /// Pure virtual function to be implemented by derived classes to handle event execution
    ///
    /// @param event_id Event identifier
    /// @param opt_data_address Optional data associated with the event
    virtual void execute_event(uint32_t event_id, uint32_t opt_data_address) = 0;

    /// Array containing the event callbacks. The index of the array indicates the event ID.
    EventCallback m_event_callback[MAX_PORT_CALLBACKS];
};

} // namespace eda

/// @}
