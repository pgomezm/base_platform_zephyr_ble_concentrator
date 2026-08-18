# Event Driven Architecture (EDA)

@defgroup grp_eda EDA
@brief Event driven architecture framework

This module provides the event-driven framework every service in this firmware is built on. It is
the Zephyr port of the active object framework used in `deepsight-polaris-software`: same
contract, different kernel primitives (`k_thread`/`k_msgq`/`k_timer` instead of the FreeRTOS
equivalents).

## Components

| Component | Responsibility |
| --- | --- |
| `ActiveObject` | One thread plus one statically allocated event queue. Runs the event loop. |
| `Port` | The address of a module. Other modules post events to a port instead of calling into it. |
| `StateMachine` / `State` | A flat state machine, one current state, explicit transitions. |
| `Timer` | Posts an event to a port when it expires. |

## Why events and not direct calls

Two modules that call each other directly share a thread, and then every constraint about
execution context in `docs/ARCHITECTURE.md` section 4 becomes something a reviewer has to verify
by hand. Posting an event makes the boundary explicit: the sender returns immediately, and the
handler is guaranteed to run in the receiving module's own thread.

## Static allocation

No `new`, no `malloc`, anywhere in this layer. Queues, stacks and thread control blocks are all
statically allocated. Queue depth is `ActiveObject::s_queue_length`; posting to a full queue drops
the event, logs it and returns `false` rather than blocking the sender.
