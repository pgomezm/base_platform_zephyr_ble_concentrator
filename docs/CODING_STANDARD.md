# Coding standard

The rules this firmware is written to, how each one is enforced, and every place it is knowingly
broken.

The second half is the point. A rule with no exceptions listed is a rule nobody has checked. Each
deviation below names what is broken, why, and what it would take to close it — which is what makes
it reviewable rather than something a reader has to discover in the code.

Companion documents: `ARCHITECTURE.md` for what the modules are and which thread runs what, and
each module's own `.md` for its contract.

---

## 1. Warnings are errors, and static analysis runs in the build

`CONFIG_COMPILER_WARNINGS_AS_ERRORS=y` in `prj.conf` makes every warning fatal across the whole
tree. `CMakeLists.txt` adds `-Wall -Wextra` to the `app` target only.

The split is deliberate. Making warnings fatal everywhere is fine, because Zephyr and the vendor
HALs already compile clean. Raising the warning *level* everywhere would fail the build on code
this project neither owns nor can fix.

Turning this on found real defects, not noise: a function pointer cast to a different signature and
called through it, which is undefined behaviour that worked by accident on this ABI.

**Not met:** no static analyser runs. See deviation D6.

## 2. Strong types

Named `enum class` rather than bare `int` or `bool`: `PostResult`, `LinkError`, `Event`,
`PortList`, `TaskPriorities`, `StateId`, `Reason`. A `bool` return that could be one of several
failures is a defect, not a style choice — a caller cannot act differently on reasons it cannot
tell apart.

**Deviation:** D2.

## 3. No raw pointers where a safer form exists, and no heap

Buffers travel as pointer plus length, with the owner named in the doc comment. `std::span` would
be the right answer and is C++20; this build is `CONFIG_STD_CPP17=y`. `std::string_view` and
`std::optional` are available and used where they fit.

No `unique_ptr`, no `shared_ptr`, no `vector`, anywhere.

**Deviation:** D2.

## 4. Explicit loops over `<algorithm>`

Nothing in `src/` includes `<algorithm>`. The loops are short, bounded by a compile-time constant,
and readable in the disassembly. In an interrupt or a timing-critical path this is not a
preference.

## 5. `auto` with restraint

Not used in any header, interface or driver signature. Used inside function bodies for iterator and
reference types where the type is on the same line.

## 6. Value semantics for small objects, no hidden allocation

Every constructor and destructor in `src/` is allocation-free. Buffers are passed by reference or
by pointer-and-length; the only large copies are `device_table::snapshot()`, which copies under a
lock on purpose so the comms thread holds no lock while it transmits.

## 7. Asserts, with a defined behaviour in release

Compile time: `static_assert` for every invariant that can be checked there — control-block sizes
in `hal/os`'s backend, and each port's event count against `MAX_PORT_CALLBACKS`.

Run time: `ASSERT_CRITICAL` in `assert/assert.hpp`, live only when the build defines
`APP_DEBUG_BUILD`.

**A release build must not lose the condition.** Every `ASSERT_CRITICAL` in this firmware is paired
with a `utils::fault::report()` that runs in both builds:

```cpp
    LOG_ERROR("critical event %u lost on port %u: %s", ...);
    utils::fault::report(to_fault_reason(result));
    ASSERT_CRITICAL(false);
```

Debug halts inside the failing function, where a debugger still has the stack. Release latches the
fault, and `svc::system_diagnostics` blinks ERROR and ACTIVITY together with the heartbeat off,
repeating the reason in the log. The watchdog keeps being fed: a device that resets itself erases
what went wrong.

**Deviations:** D1, D3.

## 8. Ownership is explicit and static

Every queue, pool, table and stack is statically allocated and sized by a Kconfig symbol or a
compile-time constant, so the memory budget can be read off the source. Who creates, who owns and
who may write is in each module's `.md`.

**Deviation:** D2.

## 9. No `using namespace` in headers

None anywhere in `src/`.

## 10. Standard library, embedded-safe subset only

`-fno-exceptions`, `-fno-rtti`, `CONFIG_REQUIRES_FULL_LIBCPP=n`. No iostream. `<cstdint>`,
`<cstddef>`, `<cstring>` and `<cstdio>` are the whole of it.

**Deviation:** D4.

## 11. Errors are return codes

Declared in `prj.conf`, not inherited from a default:

```
CONFIG_CPP_EXCEPTIONS=n
CONFIG_CPP_RTTI=n
CONFIG_REQUIRES_FULL_LIBCPP=n
```

Failures travel as `enum class` return values. `-fno-threadsafe-statics` is also on, which is why
`app::App::initialize()` constructs every singleton explicitly before the first thread starts.

## 12. Every deviation is written down

This document. If something here goes out of date, that is a defect in the document.

---

# Declared deviations

## D1 — `ASSERT_CRITICAL` is compiled out in release

**Rule:** 7.
**What:** `ASSERT_CRITICAL(x)` becomes `((void)0)` when `APP_DEBUG_BUILD` is not defined.
**Why it is accepted:** the condition is not lost. Every call site latches `utils::fault` first,
which runs in both builds and drives the LED annunciator and the log. The assert adds a halt for
the bench, not the only handling.
**To close it:** nothing, unless a halt is wanted in the field too — which would contradict the
decision that a faulted device stays alive and visible rather than resetting.
**Untested:** the release path has not been exercised on hardware. Nobody has seen the two LEDs
blink. This is the open item that matters most.

## D2 — the event system carries a pointer inside a `uint32_t`

**Rules:** 2, 3, 8.
**What:** `eda::Port::send_event(port, event_id, opt_data_address)` takes `uint32_t`. When a module
sends a struct to another module, that argument is the struct's address.
**Why it is accepted:** the targets are 32-bit ARM, where a pointer fits exactly. Typing it as
`uintptr_t` would be more correct and changes nothing on this hardware.
**The rule that goes with it:** *the pointed-to memory must be static or come from a fixed pool.
Never automatic storage.* The sender posts and returns; the receiver reads later, on another
thread. A pointer to a caller's local is read after that stack frame is gone, intermittently, and
the failure looks like corrupted data rather than a dangling pointer.
**To close it:** `uintptr_t`, and a typed wrapper if the ownership rule ever needs enforcing rather
than documenting.

## D3 — the Wi-Fi variant has no fault annunciator

**Rule:** 7.
**What:** the ESP32-S3 DevKitC defines no LED aliases, so `hal::gpio` returns absent pins and the
fault blink does nothing. On that board a latched fault is visible in the log and nowhere else.
**Why it is accepted:** declared rather than solved. The board has an addressable WS2812 on GPIO48
that could be described in an overlay, at the cost of a WS2812 driver in the HAL.
**To close it:** that driver, or a fault bit in `UplinkFlags` so the fault reaches the far end.

## D4 — the application has no heap, but the platform does

**Rule:** 10.
**What:** nothing this firmware writes allocates at run time. Zephyr's Bluetooth, LoRaWAN and
Wi-Fi subsystems do, through `HEAP_MEM_POOL_ADD_SIZE_*`. The Wi-Fi build declares 80896 bytes;
the LoRa and TCP builds declare none and genuinely have no heap.
**Why it is accepted:** the allocation belongs to vendor code that cannot be rewritten.
**To close it:** it does not close. Under IEC 62304 this is SOUP and needs its own analysis, not a
code change. `prj.conf` documents the measured figures.

## D5 — no `std::span`

**Rule:** 3.
**What:** buffers are pointer plus length.
**Why:** `std::span` is C++20; this build is C++17, set by `CONFIG_STD_CPP17=y`.
**To close it:** raise the standard, or write a minimal span. Neither is worth doing on its own.

## D6 — no static analyser in the build

**Rule:** 1.
**What:** compiler warnings are fatal, but no cppcheck, PC-lint or Polyspace runs.
**Why:** not built yet.
**To close it:** `build_flash_tools/run_lint_tool.py`, alongside the format tool. It should also
enforce mechanically what is reviewed by eye today: no RTOS name, no `k_`/`K_` symbol and no
`CONFIG_` outside `hal/*/zephyr/`, `hal/link/`, `src/config.hpp` and `src/utils/log/log.hpp`.

---

# Conventions

## Layering

`app/` → `svc/` → `hal/` → platform. A module never includes from a layer above it. `eda/` sits
beside all of them and includes none: it takes the project's port list and thread priorities from
`eda_config/`, which is what lets it be built by another project unchanged.

The RTOS is named only in `hal/*/zephyr/`, `hal/link/`, `src/config.hpp` and
`src/utils/log/log.hpp`. Everywhere else, naming it is a defect.

## Includes

Every header is included by its path from `src/`:

```cpp
#include "app/port.hpp"          // yes
#include "port.hpp"              // no, even from the same directory
```

A quoted include searches the including file's own directory first, so the short form resolves
until the day a file moves — and then it either fails somewhere unrelated or, worse, silently finds
a different file with the same name.

## Formatting

`.clang-format` at the repository root: 4-space indent, 100 columns, braces on their own line,
`SortIncludes: Never` because include order here is deliberate.

```sh
python build_flash_tools/run_format_tool.py           # fix
python build_flash_tools/run_format_tool.py --check    # report only
```

## Commit messages

Conventional Commits: `type(scope): subject`.

| type | for |
| --- | --- |
| `feat` | new behaviour |
| `fix` | **a defect corrected** |
| `refactor` | the shape changes, the behaviour does not |
| `build` | flags, Kconfig, CMake, toolchain |
| `docs` | `.md`, comments, Doxygen |
| `style` | formatting only |
| `test` | pytest and its helpers |
| `chore` | tooling, ignores, scripts |

Scopes follow the layers: `app`, `eda`, `hal`, `svc`, `utils`, `tools`.

**`fix` is the one that has to stay honest.** It means a defect was corrected, and it is what
someone auditing this repository will read first. Using it for ordinary changes empties it.

## Comments

A comment says why, not what. If it only restates the line below it, delete it. If it justifies
itself by pointing at another codebase, it does not justify itself.
