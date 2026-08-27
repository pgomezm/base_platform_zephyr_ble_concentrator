/// @addtogroup grp_eda
/// @{
///
/// @file port.hpp
///
/// Implementation of the Port class used for communication between Active Objects.

#pragma once

#include "eda_config/port_list.hpp"
#include "eda/active_object/active_object.hpp"

#include <cstdint>

/// Maximum number of distinct event ids a single Port can register a callback
/// for.
///
/// Every Port carries an array of this many function pointers, so the number
/// costs four bytes of RAM per port per slot: at 128 it was 512 B in every port
/// and 2 KB across the four this firmware has, to index event enums whose
/// longest is ten entries. Each port
/// that uses the table static_asserts its own event count against this, so
/// outgrowing it is a build failure rather than a silent drop inside
/// execute_callback().
#define MAX_PORT_CALLBACKS 32

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
    {}

    SubsystemResponse(SubsystemResultCode error, uint32_t payload)
        : error_code(error), payload_address(payload)
    {}
};

/// A Port is how one module reaches another.
///
/// A module never calls another module's functions directly for anything
/// asynchronous: it addresses an event to that module's port id, and the
/// event is handled later in the owning active object's thread context. This
/// is what keeps the execution contexts in docs/ARCHITECTURE.md section 4
/// honest.
///
/// Every port registers itself into one static table by `eda_config::PortList` id at
/// `init()` time, and `send_event()`/`send_event_from_isr()` look a port up by
/// that id rather than requiring the sender to hold a reference to it.
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
    void init(eda_config::PortList port_id, ActiveObject& active_object);

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
    static void send_event(eda_config::PortList port_id, uint32_t event_id, uint32_t opt_data_address);

    /// Send an event to a port from an ISR
    ///
    /// @param port_id Target port identifier from the PortList enum
    /// @param event_id Event identifier
    /// @param opt_data_address Optional data associated with the event
    static void send_event_from_isr(eda_config::PortList port_id,
                                    uint32_t event_id,
                                    uint32_t opt_data_address);

    /// Send an event the firmware cannot correctly continue without.
    ///
    /// Same as send_event(), except that a drop is not survivable: it is logged
    /// as an error, latched in utils::fault, and trips ASSERT_CRITICAL on a
    /// debug build.
    ///
    /// Use it when the event does not repeat. A tick or an advertising report
    /// describes a moment and another is already coming; a command or an
    /// outcome describes a change, and losing one leaves two modules disagreeing
    /// about what the device is doing forever.
    ///
    /// @param port_id Target port identifier from the PortList enum
    /// @param event_id Event identifier
    /// @param opt_data_address Optional data associated with the event
    static void send_event_critical(eda_config::PortList port_id,
                                    uint32_t event_id,
                                    uint32_t opt_data_address);

    /// Port ID
    eda_config::PortList m_port_id;

    /// Associated active object
    ActiveObject* m_active_object;

protected:
    /// Execute the callback
    ///
    /// @param event_id event identifier
    /// @param opt_data_address Optional data associated with the event
    void execute_callback(uint32_t event_id, uint32_t opt_data_address = 0);

private:
    /// Look the port up in the registry and hand it the event.
    ///
    /// A member rather than a free function because posting is ActiveObject's
    /// private business and Port is what it grants friendship to.
    ///
    /// @param port_id target port
    /// @param event_id event identifier
    /// @param opt_data_address optional data
    /// @return what became of the event
    static PostResult deliver(eda_config::PortList port_id, uint32_t event_id, uint32_t opt_data_address);

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
