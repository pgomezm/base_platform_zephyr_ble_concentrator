# Bring-up: first LoRaWAN join

How to take a freshly wired unit from powered-off to an uplink visible on the
gateway, and how to read the failures on the way. Written for the LoRa variant;
the TCP one has no equivalent procedure yet because no W5500 has been wired up.

The point of the order below is that each step tells you something the next one
cannot. Skipping to "it doesn't join" throws that away.

## 1. Wiring

The nRF52840 DK's Arduino header, with the module on SPI3. These indices were
read back from a generated devicetree, not taken from a datasheet — see
`boards/nrf52840dk_nrf52840.overlay`.

| inAir9 | DK header | nRF52840 | purpose |
| --- | --- | --- | --- |
| MISO | D12 | P1.14 | fixed by SPI3 |
| MOSI | D11 | P1.13 | fixed by SPI3 |
| SCK | D13 | P1.15 | fixed by SPI3 |
| NSS | D10 | P1.12 | chip select |
| RESET | D9 | P1.11 | active low |
| DIO0 | D2 | P1.03 | TxDone / RxDone |
| DIO1 | D3 | P1.04 | RX window timeouts |
| DIO2 | D4 | P1.05 | |
| VCC | VDD | | see below |
| GND | GND | | |

**Power.** The DK labels its 3.3 V-equivalent pin **VDD**, not `3V3`, and it is
a **fixed 3 V** buck regulator, not 3.3 V. The SX1276 runs from 1.8–3.9 V, so
3 V costs a little transmit headroom and nothing else. `SW9` selects the SoC's
source (VDD by default). Measure VDD to GND before connecting anything: it
should read about 3.0 V. Do not use the 5 V pin next to it — the inAir9 is not
5 V tolerant.

**Antenna on before power on.** The join transmits within seconds of boot, and
transmitting into an unmatched load can damage the output stage.

**All three DIO lines, not just DIO0.** The LoRaWAN stack uses DIO1 for RX
window timeouts. Without it the join starts and then hangs waiting.

**Confirm the module is an inAir9, not an inAir9B.** The B is the +20 dBm
variant and needs `power-amplifier-output = "pa-boost"` in the overlay. Setting
that wrong still builds, and transmits into the wrong output stage.

## 2. Raise the log level

In `prj_local.conf` — gitignored, applied automatically, which is what it is
for:

```
CONFIG_LOG_MODE_IMMEDIATE=y
CONFIG_LORA_LOG_LEVEL_DBG=y
CONFIG_LORAWAN_LOG_LEVEL_DBG=y

# So a dispatch does not take fifteen minutes to observe.
CONFIG_APP_DISPATCH_PERIOD_MIN=1
```

`LOG_MODE_IMMEDIATE` prints each line as it happens instead of buffering, so a
reset does not swallow the last few lines — which are always the ones that
matter. Remove all four when bring-up is done: immediate mode costs CPU, and a
one-minute dispatch period is not the product.

Console is **115200 8N1** on the lowest-numbered of the three COM ports the DK
enumerates.

## 3. Watch the gateway before powering the node

SSH to the Conduit (the US unit is at 192.168.1.45 — see `GATEWAYS.md`). On an
AEP model, SSH may need enabling first under Administration → Access
Configuration.

```sh
mosquitto_sub -v -t 'lora/#'
```

Everything, unfiltered, with `-v` so the topic prints beside the payload. Narrow
later. **Start this before powering the node**, or the join scrolls past
unobserved and the next one is not identical — the DevNonce has moved.

## 4. What a working bring-up looks like

On the console:

```
base_platform_zephyr_ble_concentrator starting
LoRa radio ready
joining: sub-band 2, DevEUI aabbccddeeff0011
joined
```

That DevEUI is derived from the SoC's factory identifier, and it is what
identifies this unit in every MQTT topic below.

On the gateway, in order:

```
lora/<DevEUI>/join_request
lora/<DevEUI>/join_accept
lora/<DevEUI>/joined
lora/<DevEUI>/up          ← then once per dispatch period
```

## 5. Reading the failures

**`join_request` is the dividing line.** If it appears, the radio works and the
gateway hears you, so the problem is credentials. If it does not appear, keys
are not worth looking at yet.

| symptom | what it means |
| --- | --- |
| Console never reaches `joining:` — `LoRa device not ready` | SPI or devicetree. Check NSS and RESET first; they are the two this project chose rather than inherited |
| `joining:` then `lorawan_join failed`, nothing in MQTT | The gateway does not hear the device. Wrong sub-band, antenna, SPI wiring, or the module is not responding |
| `join_request` and nothing after | The gateway hears you. AppEUI or AppKey do not match what the join server holds |
| `join_rejected` | Explicit refusal. Most often a repeated DevNonce, which means `CONFIG_LORAWAN_NVM_SETTINGS` is not persisting across resets |
| Board resets exactly when it transmits | Power, not firmware. The SX1276 draws over 100 mA at transmit onset; a marginal USB supply or long jumpers sag the rail enough to reset the SoC. Add 10–100 µF across the module's VDD/GND, or use a better supply |
| `joined` but no `up` | The join works; the problem is downstream in `svc::comms` or the dispatch timer |

If no `join_request` appears, the cheapest test is to change
`CONFIG_APP_LINK_LORA_SUBBAND` and rebuild. If another sub-band produces a
`join_request`, the gateway was not on the one that was configured — and that is
worth fixing in `GATEWAYS.md` rather than in the firmware.

## 6. After it joins

- Remove the four bring-up symbols from `prj_local.conf`.
- Record the unit's DevEUI somewhere alongside its physical label. It is derived
  from the SoC and cannot be chosen, so the mapping only exists if it is written
  down.
- Confirm an uplink actually carries devices: the payload on `lora/<DevEUI>/up`
  should grow as endpoints are heard, and `svc_system_diagnostics` prints a
  health line once a minute with the device count.

## 7. What the first bring-up actually found

Recorded because none of it was visible from the code, and two of the three
would have been found faster by looking at the gateway first.

**The gateway's MQTT feed is half the instrument.** `lora/net_keepalive` alone
means the broker and network server are up and nothing has transmitted. The
first `join_request` appearing is the dividing line: from that moment the radio
works and any remaining failure is credentials or downlink. Filter the keepalive
out or the join scrolls past:

```sh
mosquitto_sub -v -t 'lora/#' | grep -v net_keepalive
```

**A join that reaches the gateway and gets no answer is a key mismatch.** The
network server cannot authenticate a request whose MIC does not verify, so it
drops it silently — no `join_accept`, no `join_rejected`. On the device this
surfaces as `-116` / `Rx 2 timeout`, which reads like an RF fault and is not
one. Check `prj_local.conf` exists and that CMake printed
`-- Applying prj_local.conf` before suspecting the radio.

**A `joined` on the gateway with a timeout on the device is a one-way link.**
The gateway acknowledged and transmitted the Join-Accept in RX1 — its
`packet_sent` carries `twnd: 1` and a `tmst` exactly 5,000,000 µs after the
uplink — and the device did not hear it. That asymmetry is what a bad antenna on
the node looks like: both directions lose the same decibels, but the gateway has
the antenna, the LNA and eight receive chains to spare, so the downlink is the
one that breaks first. Uplink RSSI is the instrument; in the same room it should
be around −40 dBm, and −108 means something is wrong with the RF path
regardless of whether the join eventually succeeds.

**ADR at DR0 stops uplinks entirely, and that is not a bug.** A marginal link
makes the network server drop the device to DR0, where US915 allows an 11-byte
application payload. `UplinkHeader` is 12 bytes, so nothing fits and
`svc::comms` logs:

```
<wrn> svc_comms: dispatch skipped: 11 byte payload cannot hold a 12 byte header plus a record
```

It waits for the next cycle rather than fragmenting into packets the radio would
refuse. The fix is the antenna, not the firmware.

| DR | payload | records per fragment |
| --- | --- | --- |
| DR0 | 11 B | 0 — the header alone does not fit |
| DR1 | 53 B | 3 |
| DR2 | 125 B | 8 |
| DR3 | 242 B | 17 |

So a link good enough to join is not necessarily good enough to report. Confirm
the data rate settles above DR0 before calling a unit commissioned.
