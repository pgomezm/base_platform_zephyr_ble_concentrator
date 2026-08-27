/// @addtogroup grp_eda
/// @{
///
/// @file active_object.hpp
///
/// Header file that declares the active object.

#pragma once

#include "eda_config/tasks_priorities.hpp"
#include "hal/os/os.hpp"

#include <cstddef>
#include <cstdint>

namespace eda
{
class Port; // Forward declaration

/// What became of an event handed to a port.
enum class PostResult : uint8_t
{
    /// The event is on the queue and will be handled.
    OK,

    /// The queue was full. The event is gone.
    QUEUE_FULL,

    /// Nothing is registered at that port id, so there was nowhere to put it.
    PORT_NOT_READY,
};

/// Active Object composed by an `hal::os::Thread` and an `hal::os::Queue`.
///
/// One thread, one statically allocated event queue, nothing allocated at run
/// time and no externally supplied stack. The thread and queue are reached
/// through `hal::os` rather than an RTOS API, which is what lets this class
/// compile against any backend.
class ActiveObject
{
    friend class Port;

public:
    /// Constructor
    ActiveObject();

    /// Initialize the thread of an active object
    ///
    /// @param priority priority of the new thread
    /// @param p_task_name string to identify the thread for debugging purposes
    void init_task(eda_config::TaskPriorities priority, const char* const p_task_name);

private:
    /// @struct Event
    /// Represents an event to be sent to a port.
    ///
    /// This structure encapsulates the details of an event, including the target port,
    /// the event identifier, and optional data associated with the event.
    struct Event
    {
        /// The port instance where the event will be sent.
        Port* port;

        /// The identifier of the event, defined by the port.
        uint32_t event_id;

        /// Optional data defined by the event, commonly a pointer to an object.
        uint32_t opt_data_address;
    };

    /// Events per each active object queue
    static constexpr uint32_t s_queue_length = 20U; // 20 events per queue
    /// Size of each event in the queue
    static constexpr size_t s_queue_item_size = sizeof(Event);
    /// Total size of the queue
    static constexpr size_t s_queue_size = (s_queue_length * s_queue_item_size);

    /// The thread that runs process_events()
    hal::os::Thread m_thread;

    /// The event queue
    hal::os::Queue m_queue;

    /// Statically allocated memory for queue
    alignas(alignof(Event)) uint8_t m_task_queue_memory[s_queue_size];

    /// Post an event to the task's event queue.
    ///
    /// @param port Target Port
    /// @param event_id Event
    /// @param opt_data_address Optional data
    /// @return OK, or QUEUE_FULL if there was no room
    PostResult post_event(Port& port, uint32_t event_id, uint32_t opt_data_address);

    /// Post an event to the task's event queue from an ISR.
    ///
    /// @param port Target Port
    /// @param event_id Event
    /// @param opt_data_address Optional data
    /// @return OK, or QUEUE_FULL if there was no room
    PostResult post_event_from_isr(Port& port, uint32_t event_id, uint32_t opt_data_address);

    /// The method executed by each task in its loop
    ///
    /// @param p_active_object Pointer to the active object instance
    static void process_events(void* p_active_object);
};

/// Number of events dropped across every active object because a queue was
/// full when post_event()/post_event_from_isr() was called.
///
/// Overflow has to be observable rather than silent (docs/ARCHITECTURE.md
/// section 4), so a drop is counted as well as logged, and the post functions
/// report what happened so eda::Port::send_event_critical() can act on it.
///
/// @return the number of events dropped since boot
uint32_t get_dropped_event_count();

} // namespace eda

/// @}
