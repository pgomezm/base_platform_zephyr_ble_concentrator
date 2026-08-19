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
not against Zephyr, which is what makes a future move back to FreeRTOS a new `hal/os/freertos/`
backend instead of a rewrite of `eda/`. See §7's new `eda` / `hal/os` entry and
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
table of their last-seen sensor data, and every 15 minutes dispatches everything collected as one
or more LoRaWAN uplinks — fragmenting into multiple packets only if the current data rate's airtime
budget can't fit it all in one.

## 2. Reference: what the sensor transmits (locked to Eddystone)

From `base_platform_baremetal_ble/src/svc/eddystone/eddystone_protocol.h`:

```c
// Custom Eddystone frame (frame_type 0xFF), 19 bytes total, fits a legacy ADV (31 B max)
typedef struct __attribute__((packed)) {
    uint8_t  frame_type;      // 0xFF
    uint16_t company_id;
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
} SvcEddystoneCustomFrame;
```

No device-ID field — identity is the BLE advertiser MAC address, which `device_table` uses as the
key. `acquisition`'s parser targets this struct directly.

## 3. Board: standalone nRF52840 DevKit

Native BLE radio handles scanning. LoRa needs an external SX126x (or SX127x) module wired over SPI
— devicetree overlay describes the radio's SPI bus, `busy`/`reset`/`dio1` GPIOs, and TCXO if the
module has one. Zephyr's `CONFIG_LORA`/`CONFIG_LORAWAN` subsystem drives it from there; `hal/lora`
wraps that subsystem so `svc/comms` never touches Zephyr's API directly.

⚠️ Open, not blocking: which specific SX126x/SX127x breakout you're wiring up — the devicetree
overlay's SPI/GPIO pin assignment depends on it.

No Ethernet backend in this build. `svc/comms` keeps a small internal seam for "how to send a
built packet" (so a second backend isn't a rewrite later), but only implements the LoRa path.

## 4. Execution contexts and concurrency discipline

*(Adopted from the nRF54LM20 doc's §2–§3, adapted to what this device actually does.)*

| Context | What runs there |
| --- | --- |
| Zephyr BT scan callback | On each advertising report: check it's a BLE legacy ADV under our `company_id` filter, copy the raw payload + RSSI + advertiser address into a static pool slot, enqueue to `acquisition`'s queue. **Never parses the Eddystone frame here, never touches `device_table` directly.** |
| `acquisition` thread | Dequeues raw reports, parses `SvcEddystoneCustomFrame`, calls `device_table`'s upsert. The only thread that writes into `device_table`. |
| `comms` thread | Wakes every `DISPATCH_PERIOD_MIN`. Reads a `device_table` snapshot, builds and fragments packets, is the **only** caller of `hal/lora`'s send — single writer to the radio. |
| `system_diagnostics` thread | Heartbeat / battery / link-health checks. Lowest-priority protocol thread. |
| Zephyr log backend | Below everything else — logs never compete with scanning or the dispatch path. |

Two rules, taken directly from the nRF54LM20 doc's reasoning and just as true here:

- **Nothing of ours may run at a priority that could delay Zephyr's own BT stack thread.** All of
  our threads sit below it. This is what keeps a slow `acquisition` parse from ever causing a
  missed advertisement.
- **`hal/lora`'s send function is called from exactly one place: `svc/comms`.** No other module —
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

## 5. Uplink packet — US915 airtime math

Per-device record:

```c
struct __attribute__((packed)) EndpointRecord {
    uint8_t  mac[6];
    int8_t   rssi;
    int8_t   sns_temperature;
    uint8_t  sns_humidity;
    uint16_t battery_mv;
    uint32_t last_seen_uptime;
};  // 15 bytes — pressure/accel dropped for airtime, confirm this is still fine (open item)
```

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

### `hal/lora`

- **Owns**: the SX126x/SX127x radio via Zephyr's `CONFIG_LORA`/`CONFIG_LORAWAN` subsystem — join,
  send, US915 region config.
- **Exposes**: `join()`, `send(buffer, len)`, region/data-rate query.
- **Depends on**: Zephyr's `lora.h`/`lorawan.h` only.
- **Constraint**: `send()` is called from exactly one place — `svc/comms` (§4). No retry/backoff
  policy lives here; that's `comms`'s job, so `hal/lora` stays a thin wrapper.

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

- **Owns**: the dispatch scheduler (`DISPATCH_PERIOD_MIN` timer), packet building and
  US915-airtime fragmentation (§5), and is the sole caller of `hal/lora::send()`.
- **Exposes**: nothing — it's the top of the chain, triggered only by its own timer.
- **Depends on**: `device_table`, `hal/lora`.
- **Constraint**: single-writer to the radio (§4) — this is the one constraint in this whole
  design that, if violated, breaks silently (two contexts writing to the same SPI radio) rather
  than loudly, so it's worth restating: nothing else may call `hal/lora::send()`.

### `system_diagnostics`

- **Owns**: heartbeat and health checks (battery, `dropped_adv_reports`/`evicted_devices` counters
  from §4.1, LoRa join/link health).
- **Exposes**: the counters `comms` includes in the uplink header (§5).
- **Depends on**: `device_table` (read-only), `hal/lora` (status query only, never `send()`).

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
  (e.g., reconfigure `DISPATCH_PERIOD_MIN` over LoRaWAN), that's a new piece of design, not
  something this section's group-numbering scheme would apply to unmodified.

## 9. Still open (not blocking scaffold)

1. Exact SX126x/SX127x module + pinout for the devicetree overlay (§3).
2. LoRaWAN join mode (OTAA vs ABP) and which network server (ChirpStack, TTN, private gateway).
3. Security/encryption beyond LoRaWAN's own AppSKey.
4. Confirm the US915 payload table in §5 against the actual spec, and decide the DR0 floor
   behavior.
5. Confirm the record field list (temperature/humidity/battery/RSSI only, pressure and accel
   dropped) is actually what's needed downstream.
6. Whether the boot/reset-visibility idea in §6 is worth the wire-format change.
7. `hal/os` has one backend (Zephyr) and has only ever been exercised through it — the abstraction
   is a design bet that a FreeRTOS backend would slot in without touching `eda/`, not something
   proven by a second implementation yet.
8. `eda::IdleHook` has no callback registered anywhere in this firmware today; it exists because it
   was asked for, matching `deepsight-polaris-software`. If nothing ever registers one, it's a
   module carrying its weight for nothing, which is worth revisiting once there's an actual
   idle-time task (e.g., an LED breathe effect, or entering a lower power state) to hang off it.

## 10. Next step

Scaffold the tree from v3 §4 as real files — `west.yml`, `CMakeLists.txt`, `prj.conf`,
`boards/nrf52840dk_nrf52840.overlay` (placeholder pinout until §9.1 is answered), and stub
`hal/svc/eda/app` files with the module-contract doc-comments from §7 and the `.md` convention
from `deepsight-polaris-software`.
