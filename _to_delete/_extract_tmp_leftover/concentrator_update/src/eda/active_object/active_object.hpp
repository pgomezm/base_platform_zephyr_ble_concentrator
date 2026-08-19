/// @addtogroup grp_eda
/// @{
///
/// @file active_object.hpp
///
/// Header file that declares the active object.

#pragma once

#include "app/tasks_priorities.hpp"
#include "hal/os/os.hpp"

#include <cstddef>
#include <cstdint>

namespace eda
{
class Port; // Forward declaration

/// Active Object composed by an `hal::os::Thread` and an `hal::os::Queue`.
///
/// Ported from `deepsight-altair-software`'s FreeRTOS active object: same
/// contract (one thread, one statically allocated event queue, everything
/// else static too), same public shape (`init_task(priority, name)`, no
/// externally supplied stack). The only thing that changed is that the
/// thread and queue are reached through `hal::os` instead of the FreeRTOS API
/// directly, which is what lets this class compile against any backend
/// `hal::os` has one for.
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
    void init_task(app::TaskPriorities priority, const char* const p_task_name);

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
    void post_event(Port& port, uint32_t event_id, uint32_t opt_data_address);

    /// Post an event to the task's event queue from an ISR.
    ///
    /// @param port Target Port
    /// @param event_id Event
    /// @param opt_data_address Optional data
    void post_event_from_isr(Port& port, uint32_t event_id, uint32_t opt_data_address);

    /// The method executed by each task in its loop
    ///
    /// @param p_active_object Pointer to the active object instance
    static void process_events(void* p_active_object);
};

/// Number of events dropped across every active object because a queue was
/// full when post_event()/post_event_from_isr() was called.
///
/// Not part of `deepsight-altair-software`'s ActiveObject, which leaves a
/// dropped event as a `// TODO: handle full queue`. This concentrator's
/// architecture (docs/ARCHITECTURE.md section 4) requires overflow to be
/// observable rather than silent, so it is counted here instead of only
/// logged, while the post_event()/post_event_from_isr() signatures stay
/// `void`, matching the reference exactly.
///
/// @return the number of events dropped since boot
uint32_t get_dropped_event_count();

} // namespace eda

/// @}
