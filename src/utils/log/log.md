# Logging

@defgroup grp_utils_log Logging
@ingroup grp_utils
@brief The one place the firmware names a logging backend

## Contract

| | |
| --- | --- |
| **Owns** | The mapping from the firmware's log macros to whatever backend the platform provides. |
| **Exposes** | `LOG_MODULE_DEFINE()`, `LOG_MODULE_USE()`, `LOG_ERROR()`, `LOG_WARNING()`, `LOG_INFO()`, `LOG_DEBUG()`. |
| **Depends on** | The platform SDK only, and only inside this header. |

## Why this exists

`hal::os` exists because a call to `k_msleep()` pins a file to Zephyr. A call to `LOG_INF()` pins it
exactly as firmly, and there were 75 of them across 19 files, plus 19 `#include
<zephyr/logging/log.h>` and 19 mentions of `CONFIG_APP_LOG_LEVEL`. Logging was the largest remaining
hole in a codebase that is otherwise careful about where the platform is allowed to appear.

The names deliberately do **not** collide with Zephyr's: Zephyr uses `LOG_INF`/`LOG_WRN`/
`LOG_ERR`/`LOG_DBG`, this seam uses `LOG_INFO`/`LOG_WARNING`/`LOG_ERROR`/`LOG_DEBUG`. That is what
makes it a rename rather than a fight with the SDK's preprocessor.

## Macros, not a class

The obvious alternative is a `Logger` singleton with a swappable output backend, queuing
formatted strings and flushing them from a low-priority context. On a bare MCU with nothing but a
UART, that is what you have to write.

**Here it would be a step backwards.** Zephyr already has the same thing and does it better, and
the difference is not cosmetic:

- Zephyr's logging is *deferred* — `LOG_INFO()` stores the format string **pointer** and the
  arguments in a ring buffer, and a low-priority thread does the formatting later. A `Logger` that
  queues already-formatted text runs `vsnprintf` in the caller's context, on the caller's stack.
- A function taking `(const char* fmt, ...)` cannot be deferred. Wrapping Zephyr's macros in a
  `Logger` class would therefore throw away the property that makes them safe to call from a thread
  that must not stall — which, per `docs/ARCHITECTURE.md` §4, is most of them.
- Per-module runtime filtering, RTT and UART backends, and timestamping already exist and are
  configured through Kconfig rather than code.

So the seam is an interface over the implementation Zephyr already has. If this firmware is ever
ported to a platform with no logging subsystem, the `#else` branch of `log.hpp` is where a real
`Logger` would be wired in, and not one call site changes.

## Registration

Zephyr needs each log module declared at file scope, once for the file that owns it and once per
file that logs into it:

```cpp
#include "utils/log/log.hpp"

LOG_MODULE_DEFINE(svc_comms);   // exactly one translation unit per module
```

```cpp
LOG_MODULE_USE(app);            // the six state files all log as `app`
```

`LOG_MODULE_USE` is why the state machine's output reads as one story rather than six, and it is
the reason this seam exposes two registration macros instead of one.

The level comes from `CONFIG_APP_LOG_LEVEL` inside the macro, so no call site names a Kconfig
symbol. Per-module levels are a Zephyr feature this seam does not expose yet — every module shares
one level. Adding `LOG_MODULE_DEFINE_LEVEL(name, level)` when a module needs its own is a two-line
change here and nothing anywhere else.

## The fallback

The `#else` branch maps to `printf`. It is not meant to be good; it is meant to make the claim in
this file falsifiable — a translation unit that logs only through these names compiles on a host
with no Zephyr. Deliberately not a queue: that path is for a host build or a bare-metal bring-up,
neither of which is where logging performance is decided.
