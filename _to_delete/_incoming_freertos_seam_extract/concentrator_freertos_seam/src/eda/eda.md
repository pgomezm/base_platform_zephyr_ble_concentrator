# Event Driven Architecture (EDA)

@defgroup grp_eda EDA
@brief Event driven architecture framework

This module provides the event-driven framework every service in this firmware is built on. It is
ported millimeter-for-millimeter from `deepsight-polaris-software`'s `src/eda`, including the parts
that only make sense once other modules exist to use them (`Port::set_event_callback()`,
`StateMachine::return_to_last_state()`). The only thing that changed going from that FreeRTOS reference to this
project is the kernel underneath: `eda/` no longer includes a kernel header directly (FreeRTOS's
or Zephyr's). It includes `hal/os/os.hpp` instead, and every FreeRTOS call in the reference
(`xTaskCreateStatic`, `xQueueCreateStatic`, `xTimerCreateStatic`, ...) became a call into that
abstraction. See `hal/os/os.md` for why, and for the one seam (idle hook) that could only be
approximated on Zephyr.

## Components

| Component | Responsibility |
| --- | --- |
| `ActiveObject` | One thread plus one statically allocated event queue. Runs the event loop. |
| `Port` | The address of a module, looked up by `app::PortList` id. `send_event()`/`send_event_from_isr()` deliver to whichever port is registered at that id. |
| `StateMachine` / `State` | A state machine with transition history: `change_state()`, `return_to_last_state()`, and a two-phase `set_next_state()`/`change_to_next_state()` for conditional transitions. |
| `Timer` | Invokes a callback, on the timer's own storage, on expiry. |
| `IdleHook` | One callback invoked while the backend has nothing else to run. |

## Why events and not direct calls

Two modules that call each other directly share a thread, and then every constraint about
execution context in `docs/ARCHITECTURE.md` section 4 becomes something a reviewer has to verify
by hand. Sending an event makes the boundary explicit: the sender returns immediately, and the
handler is guaranteed to run in the receiving module's own thread.

## Addressing a port by id, not by reference

`eda::Port::send_event(app::PortList::COMMS_PORT, event_id, opt_data)` looks the target port up in
a static registry it joined at `init()` time, rather than requiring the sender to hold a reference
or pointer to it. This is `deepsight-polaris-software`'s design, carried over unchanged: it means a
module only needs `app/port_list.hpp` to reach any other module's port, not that module's header.

## Static allocation

No `new`, no `malloc`, anywhere in this layer or in `hal/os/` beneath it. Queues, stacks, timers
and port callback tables are all statically allocated. Queue depth is
`ActiveObject::s_queue_length`; posting to a full queue drops the event (counted via
`eda::get_dropped_event_count()`, per docs/ARCHITECTURE.md's no-silent-overflow rule) rather than
blocking the sender.

## Known, deliberate deviations from deepsight-polaris-software

- **`ActiveObject::post_event()`/`post_event_from_isr()` are `void`, not returning a status,
  matching the reference exactly** — but internally they increment a drop counter on a full queue
  instead of leaving a bare `// TODO: handle full queue`, since silent overflow is not acceptable
  in this project's architecture (see docs/ARCHITECTURE.md section 4.1). The signature callers see
  is unchanged.
- **`Port::send_event()`/`send_event_from_isr()` check that the target port is actually registered
  before dereferencing it.** The reference dereferences the registry slot directly in its "happy
  path" branch, which is a null-pointer dereference if a valid-range port id was never `init()`-ed.
  Fixed here; the API and the rest of the logic are unchanged.
- **`Timer`'s callback receives the `eda::Timer*` that expired, not a `TimerHandle_t`.** `hal::os`
  has no handle type with that meaning (see hal/os/os.hpp); the `Timer*` carries the same
  information a handle would (which timer fired, so `get_context()` can be called on it).
