# base_platform_zephyr_ble_concentrator — Architecture (v5)

Status: **firmware builds and links** (`prebuilt/concentrator_nrf52840dk.hex`, see the top-level
README for the verified build/flash procedure). v4 adopted a set of concurrency and documentation
conventions from the `nRF54LM20 External Device Firmware` architecture doc Pablo brought into this
discussion. That document is for a different device (a BLE instrument driven by Python over
serial, for test automation) and is **not** part of this project — but its execution-context
discipline, module-contract format, and static-memory/overflow philosophy are good practice
independent of what device they were written for, and this concentrator was missing an explicit
version of all three. What's borrowed and what's deliberately left out is called out in §4 and §8.

v5 rewrites `src/eda` to match `deepsight-polaris-software`'s `src/eda` millimeter-for-millimeter
(the static port registry addressed by `app::PortList` id, `StateMachine::change_state()`/
`return_to_last_state()`/two-phase `set_next_state()`, and the `IdleHook` module polaris has and
this project's earlier Zephyr port previously did not), and introduces `hal/os` beneath it: a seam
that wraps every kernel primitive `eda/` uses (thread, queue, timer, idle callback) so `eda/` never
includes a kernel header directly. `eda/` and everything above it is now written against `hal::os`,
not against Zephyr, which is what would make a move to another RTOS a new `hal/os/<rtos>/` backend
instead of a rewrite of `eda/`. A FreeRTOS backend was written to prove that and then removed: it
could not be compiled here, so its assertions were unverifiable, and sizing the shared storage for
it cost 320 B of RAM that nothing used. The seam stays; the speculative implementation does not. See §7's new `eda` / `hal/os` entry and
`src/eda/eda.md` / `src/hal/os/os.md` for the full rationale and the deviations from the polaris
reference (documented, not silent — same rule as §4.1's overflow counters).

`deepsight-polaris-software` is the sole reference for this project, for `src/eda` as for
everything else — an earlier draft of this section pointed at `deepsight-altair-software` for
`eda/` specifically, which turned out to be unnecessary: that repo's `src/eda` is
byte-for-byte identical to polaris's, with the sole exception of `Port::MAX_PORT_CALLBACKS`
(128 in polaris, 48 in the other). This project now uses polaris's value.

Locked decisions carried over from v3 unchanged:

| Item | Decision |
| --- | --- |
| Board / BLE topology | Standalone nRF52840 DevKit. BLE scan (native radio) + LoRa (external SPI radio module). No Ethernet, no Nucleo-F429. |
| Sensor protocol | Eddystone custom frame, exactly what `base_platform_baremetal_ble` implements today. |
| LoRaWAN region | US915 |

## 1. What the concentrator does

Runs on one Nordic nRF52840 DevKit. Passively scans for BLE endpoint devices
(`base_platform_baremetal_ble` nodes) advertising their Eddystone custom frame in a room, keeps a
table of their last-seen sensor data, and every 15 minutes dispatches what it has collected.

The table holds the **last value per device**, not a history. That single decision is what the rest
of the dispatch design rests on, and it buys two things:

1. **Only what changed goes out.** A device that has not advertised since its last successful
   uplink has nothing to add, so it is skipped. Delivery is acknowledged after the transport
   accepts the packet, not assumed at snapshot time.
2. **A cycle is not obliged to empty the table.** A device left out loses nothing, because it is
   still pending and the next cycle sends it with a fresher reading. On LoRaWAN a cycle is
   therefore **one transmission** — several packets back to back from one node is airtime it is not
   entitled to. On TCP there is nothing to ration and a cycle sends everything.

The cost is that a quiet room produces no uplink at all, which from the far end looks exactly like
a dead concentrator. A record-less heartbeat closes that gap. See §5.

## 2. Reference: what the sensor transmits (locked to Eddystone)

**Taken from what the endpoint transmits, not from its structs.** This
distinction cost a bring-up evening: the endpoint's
`eddystone_protocol.h` declares a `SvcEddystoneCustomFrame` of
`frame_type + company_id + sensor_data` = 19 bytes, this project mirrored it
byte for byte with `static_assert`s on both sides, and the two agreed perfectly
on a layout **neither one puts on the air**. The endpoint's
`build_custom_advertising_data()` never uses that struct.

What actually goes out, from `build_custom_advertising_data()` in
`base_platform_baremetal_ble/src/svc/eddystone/eddystone.c`, is ordinary BLE
advertising data — a sequence of AD structures:

```
02 01 06                       AD: Flags
13 FF                          AD: Manufacturer Specific Data, length 0x13
AA 05                          company id 0x05AA, low byte first
<16 bytes>                     SvcEddystoneSensorData
```

So the payload inside the manufacturer element is **18 bytes**, not 19, and
there is no `frame_type` field anywhere. The `0xFF` is the AD *element type* for
Manufacturer Specific Data, at offset 4 — not a frame type at offset 0.

```c
// The 18 bytes inside the Manufacturer Specific Data element
uint16_t company_id;            // 0x05AA, little-endian on the wire
struct __attribute__((packed)) {
    int8_t   sns_temperature;   // °C
    uint8_t  sns_humidity;      // %
    uint16_t sns_pressure;
    int16_t  acc_x_raw_data;
    int16_t  acc_y_raw_data;
    int16_t  acc_z_raw_data;
    uint16_t battery_mv;
    uint32_t timestamp;         // endpoint's own sequence/uptime, NOT wall clock
} sensor_data;                  // 16 bytes
```

`acquisition`'s parser therefore **walks the AD structures** looking for type
`0xFF`, rather than casting the report from offset zero. Assuming a fixed offset
is what made it read the Flags element and silently reject every endpoint in
range — the failure mode of a content filter is silence, which is why this took
a gateway capture and a side-by-side read of the transmitter to find.

No device-ID field — identity is the BLE advertiser MAC address, which
`device_table` uses as the key. The company id is a filter shared by the whole
product line, not an identity.

**The lesson is general: verifying a wire format against the peer's struct
definition is not verifying it against the wire.** Only the code that serialises
counts.

## 3. Board: standalone nRF52840 DevKit, two variants

The native BLE radio handles scanning in both variants. What differs is the module on the SPI
header, and therefore how collected readings leave the device.

| variant | module | transport | selected by |
| --- | --- | --- | --- |
| LoRa | Modtronix inAir9 (SX1276) | LoRaWAN, US915 | `CONFIG_APP_LINK_LORA` (default) |
| TCP | Wiznet W5500 | TCP to a fixed server | `CONFIG_APP_LINK_TCP` + `-S eth-w5500` |

They are mutually exclusive: both use the same four SPI pins, so the `eth-w5500` snippet deletes
the SX1276 node and the `lora0` alias that points at it.

Choosing the transport at build time rather than forking the repository is the whole reason
`hal/link` is named after the *role* and not after LoRa. Everything above it — `eda/`, the state
machine, `acquisition`, `device_table`, `comms`, `hal/ble` — is identical in both, so a fix lands
in one place.

Boards with Ethernet but no BLE radio (Nucleo F429ZI, H743ZI2) were evaluated and dropped: giving
them BLE needs an external HCI controller, whereas staying on the nRF52840 DK makes BLE a
non-problem in both variants.

## 4. Execution contexts and concurrency discipline

*(Adopted from the nRF54LM20 doc's §2–§3, adapted to what this device actually does.)*

| Context | What runs there |
| --- | --- |
| Zephyr BT scan callback | On each advertising report: check it's a BLE legacy ADV under our `company_id` filter, copy the raw payload + RSSI + advertiser address into a static pool slot, enqueue to `acquisition`'s queue. **Never parses the Eddystone frame here, never touches `device_table` directly.** |
| `acquisition` thread | Dequeues raw reports, parses the manufacturer AD element, calls `device_table`'s upsert. The only thread that writes into `device_table`. |
| `comms` thread | Wakes every `DISPATCH_PERIOD_S`. Reads a `device_table` snapshot, builds and fragments packets, is the **only** caller of `hal/link`'s send — single writer to the radio. |
| `system_diagnostics` thread | Heartbeat / battery / link-health checks. Lowest-priority protocol thread. |
| Zephyr log backend | Below everything else — logs never compete with scanning or the dispatch path. |

Two rules, taken directly from the nRF54LM20 doc's reasoning and just as true here:

- **Nothing of ours may run at a priority that could delay Zephyr's own BT stack thread.** All of
  our threads sit below it. This is what keeps a slow `acquisition` parse from ever causing a
  missed advertisement.
- **`hal/link`'s send function is called from exactly one place: `svc/comms`.** No other module —
  not a future debug command, not `system_diagnostics` — calls it directly. One writer means no
  interleaved access to the radio's SPI bus and no ambiguity about transmit order, the same reason
  the nRF54LM20 design gives every UART write a single owning thread.

**Nothing that matters happens in the BT scan callback.** This is the concentrator's version of
"nothing that matters happens in a Bluetooth callback" from that doc: the callback's only job is
copy-and-enqueue. Parsing, RSSI-based filtering logic, and any decision about what counts as a
duplicate/updated reading all happen in `acquisition`'s own thread context, not inline in the
Zephyr BT stack's callback.

### 4.1 Static memory and observable overflow

No runtime allocation anywhere in `svc`/`hal` — every buffer is a static pool or fixed-capacity
table, sized by a Kconfig symbol, matching the nRF54LM20 doc's "every size is a Kconfig symbol,
because those sizes are the visible limit on what [the system] may declare." Two concrete
overflow points, each with an explicit, observable policy instead of the vague "eviction/aging"
v3 left this at:

- **Raw-report pool** (between the BT scan callback and `acquisition`): fixed size
  (`CONFIG_APP_ADV_REPORT_POOL_SIZE`). If a burst of simultaneous advertisers fills it before
  `acquisition` drains it, the callback drops the new report and increments a
  `dropped_adv_reports` counter.
- **`device_table`** (fixed `MAX_DEVICES`, default 100): if a new, previously-unseen MAC arrives
  once the table is full, evict the entry with the oldest `last_seen_uptime` to make room, and
  increment an `evicted_devices` counter. Both counters are exposed via `system_diagnostics` and
  worth including in the uplink so a network operator can tell "this concentrator saw more devices
  than it could track" rather than silently under-reporting a crowded room.

## 4.2 The concentrator decides nothing about the data

**It receives advertisements and dispatches them. That is the whole contract.**

No threshold, no motion detection, no filtering on sensor values, no derived field, no aggregation
window, no opinion about what an accelerometer reading means. Whatever consumes the uplinks makes
those decisions, where there is a database, a history and a person who can change their mind
without a firmware release.

The three decisions the concentrator *does* make are all about the link, not the data:

| Decision | Why it cannot live anywhere else |
| --- | --- |
| Last value per device, not every advertisement | 20 endpoints at 1 Hz is 22.5 MB/day. LoRa carries roughly four orders of magnitude less. Something has to sample, and only the device holding the radio knows what fits. |
| How many packets per cycle | Airtime is a property of the transport (§5). |
| Which devices are stale | An entry no advertisement has refreshed is absence of input, not an interpretation of it. |

None of those look at a sensor value. They look at the clock and at the radio.

This is why the endpoint firmware is not being modified: the concentrator has no need for the
endpoint to pre-digest anything, because it does not digest anything itself. It also rules out an
attractive-looking optimisation — computing motion from `acc_x/y/z` here to send one byte instead
of six — and that is deliberate. It would be inventing a threshold on the device that is hardest to
change, from 1 Hz samples of a sensor already running at 100 Hz, and it would put a product
decision inside a relay.

### Two products, one firmware

| | LoRa build | TCP build |
| --- | --- | --- |
| Application | Industrial: motors running or stopped, temperature, humidity | Buildings and city: is this machine in use right now |
| Dispatch period | 900 s | 30 s |
| Uplinks per cycle | 3 | unbounded |
| Deployment | Its own endpoints and its own concentrators | Its own endpoints and its own concentrators |

They are separate installations that never see each other. The only thing they share is this
repository, and the only file that knows which one is being built is `hal/link`.

## 5. Uplink packet — US915 airtime math

Per-device record:

```c
struct __attribute__((packed)) EndpointRecord {
    uint8_t  address[6];          // added here: identity
    int8_t   rssi;                // added here: a property of THIS link
    uint16_t seconds_since_seen;  // added here: no wall clock on the device

    int8_t   temperature;         // ---- the endpoint's payload, verbatim ----
    uint8_t  humidity;
    uint16_t pressure;
    int16_t  acc_x;
    int16_t  acc_y;
    int16_t  acc_z;
    uint16_t battery_mv;
    uint32_t endpoint_timestamp;
};  // 25 bytes: 9 added by the concentrator, 16 relayed unchanged
```

**Open item closed.** An earlier version dropped pressure and the three accelerometer axes to save
airtime. That was a decision about the *content* of the data, and §4.2 says the concentrator does
not make those. Dropping the axes made the accelerometer unusable on the far end, which is the
field the industrial application cares about most, so the record now carries the payload through
whole.

The cost is one number: at DR3 a fragment holds about 9 records instead of 15. Nothing is lost by
that, since unreported devices stay pending, and it is paid for by `APP_LINK_LORA_MAX_UPLINKS_PER_DISPATCH`
going from 1 to 3 — about 1.2 s of airtime every 900 s, or 0.13%.

Header: `concentrator_id`(4) + `sequence`(2) + `fragment_index`(1) + `fragment_count`(1) +
`record_count`(1) + `dropped_adv_reports`(1) + `evicted_devices`(1) = 11 bytes (two bytes added
for the overflow counters from §4.1 — small cost for making data loss observable, per the same
principle the nRF54LM20 doc states directly: "asynchronous data loss is observable... so a test
can invalidate its own result rather than publish a wrong one." Here it's an operator deciding
whether to trust an occupancy count, not a test, but the reasoning is identical).

US915 max application payload by data rate (from memory of the LoRaWAN Regional Parameters
US902-928 table — **verify against the current spec / Zephyr's region table before relying on
these**, and note US915 also has a dwell-time rule that can shrink DR0/DR1 further):

| Data rate | Approx. max payload | Header (11B) leaves room for | Records/fragment |
| --- | --- | --- | --- |
| DR0 (SF10/125kHz) | ~11 B | 0 B | 0 — too small for even one record at this DR |
| DR1 (SF9/125kHz) | ~53 B | ~42 B | 2 |
| DR2 (SF8/125kHz) | ~125 B | ~114 B | 7 |
| DR3 (SF7/125kHz) | ~242 B | ~231 B | 15 |

If DR0 really can't carry even one record, `comms` needs a floor — either force a minimum data
rate via ADR configuration, or explicitly treat "can't send anything at this DR" as a wait state
rather than an infinite fragment loop.

**Resolved: it is a wait state.** `comms` skips the cycle and logs it. Confirmed on hardware — a
join succeeds, ADR settles at DR0 because the link budget is bad, and every dispatch logs
`dispatch skipped`. The guard is behaving correctly; what is wrong is the antenna, not the
firmware. See `docs/BRINGUP.md`.

**Also resolved: how many packets one cycle may send.** The table above answers how *big* a packet
may be, which is not the same as how *many* may leave at once. `hal::link` answers that separately
through `get_max_uplinks_per_dispatch()` — 1 on LoRaWAN, unbounded on TCP — and `comms` sends the
smaller of "fragments needed" and "fragments allowed", resuming next cycle from where it stopped.

The number that matters operationally is not the packet count but the resulting report interval:
with 30 pending devices at DR3 (9 records/fragment at 25 bytes each) a pass needs 4 fragments and
the allowance is 3, so the tail of the room waits for the next cycle. Shorten `APP_DISPATCH_PERIOD_S` or fix the
link budget; raising `APP_LINK_LORA_MAX_UPLINKS_PER_DISPATCH` trades a duty-cycle problem for a
latency one and should not be done without measuring the airtime.

Nothing tracks where a cycle stopped. A record is acknowledged only once the fragment carrying it
was accepted, so whatever did not go out is still pending and the next snapshot leads with it —
exact rather than approximate fairness, and one less piece of state.

**Silence is not free.** Reporting only what changed means an idle room sends nothing, which at the
far end is indistinguishable from a failed device. After `APP_HEARTBEAT_AFTER_CYCLES` quiet cycles
(default 4) the concentrator sends a header with no records and the `HEARTBEAT` flag set, carrying
the dropped-report and eviction counters. It is 12 bytes, so it fits at every data rate above DR0.

Note the header is **12 bytes**, not the 11 written above: a `flags` byte was added for the boot
marker in §6. At DR0 that is the difference between 0 and 0 records, so it changes nothing there,
but the table's "leaves room for" column is one byte optimistic at every rate.

## 6. State machine

`Concentrator_StatusDiagram.drawio` and the `STARTED → HIRING → COLLECT DATA → SEND DATA` loop
from v2 §9 apply unchanged — none of that was board- or protocol-specific.

One addition worth considering, adapted from the nRF54LM20 doc's boot event ("everything you knew
about me is gone" — the one signal that tells the far end a reset happened rather than making it
infer one from silence): have the concentrator's first dispatch after boot carry a `boot` flag and
`reset_reason` in place of/alongside normal readings, so whoever consumes the uplinks on the
network-server side knows not to assume continuous occupancy history across that gap. Not designed
in detail here — flagging it as a genuinely useful idea from that doc, pending your call on whether
it's worth the wire-format change.

## 7. Module reference (contract format, adopted from the nRF54LM20 doc's §6)

Each module states what it **owns**, what it **exposes**, what it **depends on**, and any
constraint that isn't free to change — replaces the plain description table from v3.

### `hal/ble`

- **Owns**: the nRF52840's native BLE radio in Observer role — scan start/stop, the raw
  advertising-report callback described in §4.
- **Exposes**: `start_scan()` / `stop_scan()`, and a registration point for the raw-report
  callback that `acquisition` consumes from.
- **Depends on**: Zephyr's Bluetooth API only.
- **Constraint**: never parses payload content, never filters beyond a company-ID check. Anything
  Eddystone-specific belongs in `acquisition`, not here — this is what keeps `hal/ble` reusable if
  the frame format ever changes.

### `hal/link`

- **Owns**: the uplink transport, whichever one this build compiled.
- **Exposes**: `ILink` through `LinkFactory::get_instance()` — `initialize()`, `connect()`,
  `is_connected()`, `send()`, `register_downlink_callback()`, `get_max_payload_size()`.
- **Depends on**: the platform SDK only.
- **Backends**: `lora/link_lora.cpp` over Zephyr's `CONFIG_LORAWAN`, and `tcp/link_tcp.cpp` over
  Zephyr sockets. `CMakeLists.txt` compiles exactly one, and the Kconfig choice also selects the
  Zephyr subsystem that backend needs — which is why `prj.conf` names neither LoRa nor networking.
- **Constraint**: `send()` is called from exactly one place — `svc/comms` (§4). No retry or backoff
  policy lives here; that is `comms`'s job, and the state machine's, so a backend stays a thin
  wrapper. A failed TCP `send()` drops the socket and reports; it does not reconnect behind
  anyone's back.
- **Nothing above this module may name a transport.** `get_max_payload_size()` on TCP reports a
  limit TCP does not have (the LoRaWAN ceiling, by Kconfig) precisely so `comms` fragments
  identically on both and there is one fragmentation path to test.

### `hal/gpio`, `hal/led`, `hal/watchdog`

- **`hal/gpio`** owns one `IGpio` per pin the firmware drives, handed out by
  `ManagerFactory::get_instance()`. Pins are configured once, in the manager's constructor.
- **`hal/led`** wraps a GPIO as an LED (`turn_on`/`turn_off`/`toggle`), handed out by
  `Manager::get_instance()`. It has **no platform subdirectory**: every platform-specific line is
  behind `IGpio`, which is what splitting GPIO out bought.
- **`hal/watchdog`** exposes `IWatchdog` through `WatchdogFactory::get_instance()`. `refresh()` has
  exactly one caller, `svc/system_diagnostics`: a module that refreshes from its own thread proves
  that one thread is alive, not that the firmware is.

### `utils/log`

- **Owns**: the mapping from the firmware's log macros to the platform's logging backend. It is the
  only file in `src/` that includes `zephyr/logging/log.h`.
- **Exposes**: `LOG_MODULE_DEFINE()`, `LOG_MODULE_USE()`, `LOG_ERROR()`, `LOG_WARNING()`,
  `LOG_INFO()`, `LOG_DEBUG()` — the names `deepsight-polaris-software` uses, so a file reads the
  same in both repositories.
- **Why it is macros and not polaris's `Logger` class**: Zephyr's logging is deferred, storing the
  format string pointer and the arguments rather than formatted text, so nothing is formatted in
  the caller's context. A function taking `(const char* fmt, ...)` cannot be deferred, so wrapping
  the macros in a class would throw that away — and §4 says most of these call sites are on threads
  that must not stall. The seam is polaris's interface over Zephyr's implementation. See
  `src/utils/log/log.md`.
- **Constraint**: no other file in `src/` may name a logging header, a log backend, or
  `CONFIG_APP_LOG_LEVEL`. Same rule as `hal::os`, and for the same reason.

### `acquisition`

- **Owns**: the BLE scan lifecycle and the Eddystone frame parser (§2). The only writer into
  `device_table`.
- **Exposes**: nothing upward besides what it writes into `device_table` — no other module calls
  into `acquisition` directly.
- **Depends on**: `hal/ble`, `device_table`.
- **Constraint**: runs in its own thread context (§4), never inline in the BT scan callback.

### `device_table`

- **Owns**: the fixed-capacity (`MAX_DEVICES`, default 100) table of last-known readings, keyed by
  BLE MAC, plus the eviction counters from §4.1.
- **Exposes**: `upsert(mac, reading)` (called only by `acquisition`), `snapshot()` (called only by
  `comms` at dispatch time, returns a read-only copy so `comms` never blocks `acquisition`).
- **Depends on**: nothing besides `common`/`utils` — no hardware, no Bluetooth or LoRa headers.
- **Constraint**: not its own thread — a mutex-guarded static structure, matching the nRF54LM20
  doc's `conn` registry pattern (owned by the threads that use it, not a thread itself).

### `comms`

- **Owns**: the dispatch scheduler (`DISPATCH_PERIOD_S` timer), packet building and
  US915-airtime fragmentation (§5), and is the sole caller of `hal::link::send()`.
- **Exposes**: nothing — it's the top of the chain, triggered only by its own timer.
- **Depends on**: `device_table`, `hal/link`.
- **Constraint**: single-writer to the radio (§4) — this is the one constraint in this whole
  design that, if violated, breaks silently (two contexts writing to the same SPI radio) rather
  than loudly, so it's worth restating: nothing else may call `hal::link::send()`.

### `system_diagnostics`

- **Owns**: heartbeat and health checks (battery, `dropped_adv_reports`/`evicted_devices` counters
  from §4.1, LoRa join/link health).
- **Exposes**: the counters `comms` includes in the uplink header (§5).
- **Depends on**: `device_table` (read-only), `hal/link` (status query only, never `send()`).

### `eda` / `hal/os`

- **Owns**: the active-object framework every module above it runs on (`ActiveObject`, `Port`,
  `StateMachine`/`State`, `Timer`, `IdleHook`), and, in `hal/os` beneath it, the thread/queue/timer
  primitives that framework is built from.
- **Exposes**: `eda::Port::send_event()`/`send_event_from_isr()` (address any module's port by its
  `app::PortList` id), `eda::Timer`, `eda::IdleHook::register_callback()`.
- **Depends on**: `hal/os` only — not Zephyr directly. `hal/os`'s Zephyr backend is the one place
  in the firmware that includes `<zephyr/kernel.h>` for thread/queue/timer purposes.
- **Constraint**: no `new`, no `malloc` (same static-allocation rule as everywhere else); a
  `hal::os::Thread`/`Queue`/`Timer` is a fixed-size byte buffer sized for the backend's control
  block, `static_assert`-checked in the backend's `.cpp`.
- **Why it exists**: `eda/` is ported from `deepsight-polaris-software`, which is FreeRTOS
  firmware. `hal/os` is the seam that lets `eda/` be identical to that reference while running on
  Zephyr — see `src/hal/os/os.md`.
- **Known deviation**: Zephyr has no public idle-hook equivalent to FreeRTOS's
  `vApplicationIdleHook()`. `hal::os::register_idle_callback()` approximates one with a dedicated,
  lowest-priority thread (see `os_zephyr.cpp`). `eda::IdleHook` exists and is wired up, with no
  callback registered by default.
- **One backend, on purpose**: a FreeRTOS implementation of this same interface was written and
  then deleted. It could not be compiled here, so its `static_assert`s were unverifiable, and
  sizing `QueueStorage`/`TimerStorage` to fit both backends cost 320 B of RAM for a backend nothing
  ran. Treat that as the precedent for any "write it now, build it later" proposal: `hal/link`'s
  TCP backend earns its place because it compiles and is verified on every change.

## 8. What's deliberately not adopted from the nRF54LM20 doc, and why

Worth stating explicitly rather than leaving silent, per that doc's own principle ("saying so is
worth a sentence, because silence about a whole transport reads as coverage rather than as a
decision"):

- **No command router / `serial_if` / pending-transaction-per-command layer.** That doc's firmware
  is driven live by a Python host issuing synchronous-feeling commands over an asynchronous stack
  — hence transaction numbers, `PENDING`/`DONE` handlers, per-slot timeouts. This concentrator has
  no live host driving it; its only inputs are passive BLE scanning and its own 15-minute timer.
  There's no RPC surface, so there's nothing for a transaction ID to track.
- **No GATT server / Peripheral role / connection registry.** The concentrator only scans
  (Observer role) — it never advertises, never accepts a connection, never runs as a GATT client
  or server. The nRF54LM20 doc's `conn` (multi-role connection registry) and `gatt_server`
  (runtime-declared GATT table) modules have no counterpart here.
- **No wire-protocol group/versioning scheme for an extensible command grammar.** The LoRa uplink
  is one fixed telemetry frame type (§5), not an RPC protocol that needs `mux`-style extension
  points for new command groups. If the concentrator ever needs to *receive* downlink commands
  (e.g., reconfigure `DISPATCH_PERIOD_S` over LoRaWAN), that's a new piece of design, not
  something this section's group-numbering scheme would apply to unmodified.

## 9. Still open

Ordered by what blocks what. Item 1 is the only one standing between this firmware and end-to-end
operation.

1. **LoRaWAN join mode (OTAA vs ABP) and the network server.** OTAA is the decision on the table:
   the device has a watchdog and will reboot, and ABP requires the frame counter to survive that or
   the network server silently discards every uplink as a replay. Two things fall out of it that
   are easy to miss:
   - Zephyr's stack does **not** retry a failed join. `lorawan_join()` attempts once and returns an
     errno, so retrying is this firmware's job — which is what §6's `SOFT_ERROR` path already does.
   - OTAA on LoRaWAN 1.0.4 needs a monotonically increasing DevNonce across reboots, so it has to
     live in non-volatile storage (`CONFIG_LORAWAN_NVM_SETTINGS`).
   - In US915 the gateway uses one sub-band of eight. `lorawan_set_channels_mask()` must be called
     before the join or the device hunts 72 channels and appears to fail for RF reasons it does not
     have. The gateway here is a MultiTech Conduit; its configured sub-band is the value to match.

2. **The W5500 is not wired up.** `snippets/eth-w5500/w5500.overlay` carries the only pin
   assignments in this project not read back from a generated devicetree. The TCP backend compiles
   in both the static-address and DHCP configurations and has never run.

3. **TCP downlink is accepted but never delivered.** `register_downlink_callback()` stores the
   pointer; delivering would need a reader thread parked in `zsock_recv()`, and what framing means
   over a stream is undecided.

4. Security beyond LoRaWAN's own AppSKey, and anything at all on the TCP side.

5. Confirm the US915 payload table in §5 against the actual spec, and decide the DR0 floor
   behaviour.

6. Confirm the record field list (temperature/humidity/battery/RSSI only, pressure and accel
   dropped) is what is actually needed downstream.

7. Whether the boot/reset-visibility idea in §6 is worth the wire-format change.

8. `eda::IdleHook` has no callback registered anywhere today; it exists because it was asked for,
   matching `deepsight-polaris-software`. If nothing ever registers one it is a module carrying its
   weight for nothing, worth revisiting once there is an actual idle-time task to hang off it.

## 10. Where this stands

The scaffold is done and the firmware builds in three configurations: LoRa, TCP with a static
address, and TCP with DHCP.

| variant | FLASH | RAM |
| --- | --- | --- |
| LoRa | 120 KB (11.5%) | 55 KB (21.0%) |
| TCP | 155 KB (14.8%) | 80 KB (30.5%) |

What is exercised: the whole BLE path — passive scan, Eddystone parsing, the device table, the
state machine, the watchdog, the LEDs. What is not: either transport. The device boots, scans,
fills the table, and falls into `SOFT_ERROR` when it tries to dispatch, because `connect()`
deliberately fails.

The next step is item 1 above.
