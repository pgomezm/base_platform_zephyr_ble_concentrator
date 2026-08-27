# Event Driven Architecture (EDA)

@defgroup grp_eda EDA
@brief Event driven architecture framework

This module provides the event-driven framework every service in this firmware is built on.
Nothing in it includes a kernel header. It includes `hal/os/os.hpp` instead, and every thread,
queue and timer it needs is a call into that abstraction, which is what lets the whole layer move
to another RTOS by writing one backend rather than by being rewritten. See `hal/os/os.md` for the
one seam — the idle hook — that could only be approximated on Zephyr.

A few pieces are here before anything uses them, `StateMachine::return_to_last_state()` among
them. They are part of the framework's shape and cost nothing until a module reaches for one.

## Components

| Component | Responsibility |
| --- | --- |
| `ActiveObject` | One thread plus one statically allocated event queue. Runs the event loop. |
| `Port` | The address of a module, looked up by `eda_config::PortList` id. `send_event()`/`send_event_from_isr()` deliver to whichever port is registered at that id; `send_event_critical()` refuses to lose the event quietly. |
| `StateMachine` / `State` | A state machine with transition history: `change_state()`, `return_to_last_state()`, and a two-phase `set_next_state()`/`change_to_next_state()` for conditional transitions. |
| `Timer` | Invokes a callback, on the timer's own storage, on expiry. |
| `IdleHook` | One callback invoked while the backend has nothing else to run. |

## Events that may be lost, and events that may not

`post_event()` puts the event on the target's queue and returns. The queue holds 20 events per
active object, and a full queue means the event is dropped: counted in `get_dropped_event_count()`,
logged, and gone.

Counting is enough for most events and not enough for some, and the line between them is whether
the event **repeats**:

| | example | if it is lost |
| --- | --- | --- |
| Periodic or interrupt-driven | `DISPATCH_DUE`, `ADV_REPORT_AVAILABLE`, `HEARTBEAT_DUE` | one moment is missed; another is already on its way |
| One-shot command or outcome | `START_DISPATCH`, `STOP_SCAN`, `NETWORK_JOINED` | two modules disagree about what the device is doing, permanently, with nothing to correct it |

`STOP_DISPATCH` is the one worth naming: lose it and a concentrator that has entered `HARD_ERROR`
keeps transmitting, and the counter in the uplink header says so only to whoever reads it.

So one-shot events go out through `Port::send_event_critical()`. It posts exactly as `send_event()`
does; the difference is what happens when the queue is full. The drop is logged as an error rather
than a warning, and `ASSERT_CRITICAL` halts the device on a debug build — where, with the watchdog
running, halting means a reset a few seconds later with a watchdog reset reason. See `assert/`.

**This is a detector, not a fix.** `ASSERT_CRITICAL` compiles out of a release build, so in the
field a lost critical event is still a lost critical event, now with an error in the log. The real
answer is either a `bool` return that every caller handles, or making the state something the other
module can read rather than something it has to be told once. Both are open; this is what stops the
condition from being invisible in the meantime.

## The callback table

Every `svc` port ends its `execute_event()` with `execute_callback()`, exactly as the reference
does. The switch above it is what the service *does* with the event; the callback is how a module
that is not the service learns the event happened, without the service having to know that module
exists. `app::Port` does not call it, also matching the reference: callbacks are registered on
service ports, by the state machine, not on the application's own port.

`MAX_PORT_CALLBACKS` is 32 here rather than the reference's 128. Each port carries an array of that
many function pointers, so at 128 it was 512 B per port and 2 KB across four ports, indexing event
enums whose longest is ten entries. Each service port `static_assert`s its own event count against
the limit, so outgrowing it fails the build instead of silently dropping the callback inside
`execute_callback()`'s bounds check.

## Why events and not direct calls

Two modules that call each other directly share a thread, and then every constraint about
execution context in `docs/ARCHITECTURE.md` section 4 becomes something a reviewer has to verify
by hand. Sending an event makes the boundary explicit: the sender returns immediately, and the
handler is guaranteed to run in the receiving module's own thread.

## Addressing a port by id, not by reference

`eda::Port::send_event(eda_config::PortList::COMMS_PORT, event_id, opt_data)` looks the target port up in
a static registry it joined at `init()` time, rather than requiring the sender to hold a reference
or pointer to it. That way a module only needs `app/port_list.hpp` to reach any other module's
port, not that module's header.

## Static allocation

No `new`, no `malloc`, anywhere in this layer or in `hal/os/` beneath it. Queues, stacks, timers
and port callback tables are all statically allocated. Queue depth is
`ActiveObject::s_queue_length`; posting to a full queue drops the event (counted via
`eda::get_dropped_event_count()`, per docs/ARCHITECTURE.md's no-silent-overflow rule) rather than
blocking the sender.

## Three decisions worth knowing about

- **`post_event()`/`post_event_from_isr()` return a `PostResult`, not `void` or a bare `bool`.** A
  full queue has to be distinguishable from a port nobody registered, because the two mean
  different things to `send_event_critical()` and land as different reasons in `utils::fault`.
  Silent overflow is not acceptable here (docs/ARCHITECTURE.md section 4.1), so the drop is
  counted and logged as well.
- **`send_event()`/`send_event_from_isr()` check that the target port is registered before
  dereferencing it.** A port id that is in range but was never `init()`-ed would otherwise be a
  null pointer dereference.
- **`Timer`'s callback receives the `eda::Timer*` that expired, not a backend handle.** `hal::os`
  exposes no handle type (see hal/os/os.hpp), and the pointer carries the same information: which
  timer fired, so `get_context()` can be called on it.
