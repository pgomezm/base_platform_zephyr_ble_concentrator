/// @addtogroup grp_eda
/// @{
///
/// @file active_object.hpp
///
/// Header file that declares the active object.

#pragma once

#include "app/tasks_priorities.hpp"

#include <zephyr/kernel.h>

#include <cstddef>
#include <cstdint>

namespace eda
{
class Port; // Forward declaration

/// Active Object composed by a Zephyr thread and a message queue.
///
/// This is the Zephyr port of the FreeRTOS active object used in
/// deepsight-polaris-software. The contract is identical: one thread, one
/// event queue, all memory statically allocated. Only the kernel primitives
/// changed (`k_thread`/`k_msgq` instead of `xTaskHandle`/`xQueueHandle`).
class ActiveObject
{
    friend class Port;

public:
    /// Events per each active object queue
    static constexpr uint32_t s_queue_length = 20U;

    /// Stack size for all threads, in bytes
    static constexpr size_t s_stack_size = 2048U;

    /// Constructor
    ActiveObject();

    /// Initialize the thread of an active object
    ///
    /// @param priority priority of the new thread
    /// @param p_task_name string to identify the thread for debugging purposes
    /// @param p_stack statically allocated stack, declared with K_THREAD_STACK_DEFINE
    /// @param stack_size size of @p p_stack in bytes
    void init_task(app::TaskPriorities priority, const char* const p_task_name,
                   k_thread_stack_t* p_stack, size_t stack_size);

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

    /// Size of each event in the queue
    static constexpr size_t s_queue_item_size = sizeof(Event);

    /// Total size of the queue, in bytes
    static constexpr size_t s_queue_size = (s_queue_length * s_queue_item_size);

    /// Control block for the thread
    struct k_thread m_thread_data;

    /// Handle of the thread
    k_tid_t m_thread_id;

    /// Event queue of the thread
    struct k_msgq m_queue;

    /// Statically allocated memory for the queue.
    ///
    /// Aligned so `k_msgq_init()` accepts it for any Event layout.
    alignas(alignof(Event)) uint8_t m_task_queue_memory[s_queue_size];

    /// Post an event to the thread's event queue.
    ///
    /// Safe to call from thread context. Never blocks: if the queue is full the
    /// event is dropped and reported, per the overflow policy in
    /// docs/ARCHITECTURE.md section 4.1.
    ///
    /// @param port Target Port
    /// @param event_id Event
    /// @param opt_data_address Optional data
    /// @return true if the event was queued, false if the queue was full
    bool post_event(Port& port, uint32_t event_id, uint32_t opt_data_address);

    /// Post an event to the thread's event queue from an ISR.
    ///
    /// Identical to post_event() on Zephyr: `k_msgq_put()` with `K_NO_WAIT` is
    /// ISR-safe. Kept as a separate entry point so call sites still document
    /// the context they run in.
    ///
    /// @param port Target Port
    /// @param event_id Event
    /// @param opt_data_address Optional data
    /// @return true if the event was queued, false if the queue was full
    bool post_event_from_isr(Port& port, uint32_t event_id, uint32_t opt_data_address);

    /// The method executed by each thread in its loop
    ///
    /// @param p_active_object Pointer to the active object instance
    /// @param p_unused1 Unused, required by the Zephyr thread entry signature
    /// @param p_unused2 Unused, required by the Zephyr thread entry signature
    static void process_events(void* p_active_object, void* p_unused1, void* p_unused2);
};

} // namespace eda

/// @}
