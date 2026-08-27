# EDA configuration

@defgroup grp_eda_config EDA configuration
@brief The project's half of the contract with `eda/`

## Contract

| | |
| --- | --- |
| **Owns** | The list of ports this firmware has, and the priority of each thread. |
| **Exposes** | `eda_config::PortList`, `eda_config::TaskPriorities`. |
| **Depends on** | Nothing. |

## Why this directory exists

`eda/` used to include `app/port_list.hpp` and `app/tasks_priorities.hpp`. That is backwards: the
framework was including the application, so `eda/` could not be built without this exact project's
list of four ports and four priorities.

Now `eda/` includes `eda_config/port_list.hpp` and `eda_config/tasks_priorities.hpp`. The header
path and the type names belong to `eda/`; the values in them belong to whoever is building. Any
project that provides these two headers on its include path can compile `eda/` unchanged.

Nothing else changed. The enums are the same enums, moved and renamed.

## What a second project would have to supply

Copy this directory, and edit both enums:

- **`PortList`** — one entry per module owning an `eda::Port`, plus `INVALID_PORT` first and
  `PORT_COUNT` last. `eda::Port` sizes its static registry from `PORT_COUNT`, so the sentinel is
  not decoration.
- **`TaskPriorities`** — one entry per active object. Lower number is higher priority. Mapping
  these onto a backend's own scheme is `hal::os`'s problem, not this file's.

## What is still not configurable

Four constants that belong to a project but are still hardcoded inside the shared layer:

| where | what | today |
| --- | --- | --- |
| `eda/port/port.hpp` | `MAX_PORT_CALLBACKS` | 32 |
| `eda/active_object/active_object.hpp` | `s_queue_length` | 20 |
| `hal/os/os.hpp` | `Thread::STACK_SIZE` | 2048 |
| `hal/os/zephyr/os_zephyr.cpp` | `MAX_THREADS` | 8 |

Every one of those was chosen by reasoning about *this* firmware — `MAX_PORT_CALLBACKS` came down
from 128 to 32 by counting this project's event enums. A shared component has no business deciding
them. Moving them here is the next step, and it is what stands between this and `eda/` being a
submodule.

## Why a submodule and not a west module

`hal/os` exists so that `eda/` is not bound to one RTOS. Packaging the layer as a west module would
bind it to the Zephyr ecosystem instead, which trades one lock-in for another. A plain git
submodule with its own `CMakeLists.txt` can be consumed by a Zephyr build and by a bare-metal
FreeRTOS one alike.
