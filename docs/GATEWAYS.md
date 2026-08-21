# Gateways

The MultiTech Conduits this concentrator is tested against, and what each one's
configuration means for the firmware build.

> **Identifiers.** Serial numbers, IMEI and MAC addresses are recorded below
> because they identify which physical unit a measurement came from. If this
> repository is or becomes public, decide whether they belong here — nothing in
> the firmware reads them.
>
> **Keys are not in this file, and must not be.** The AppKey lives in a
> gitignored local config; see "Credentials" at the end.

## US unit — 915 MHz

### Device

| | |
| --- | --- |
| Model | MTCDT-H5-210L |
| Serial | 18440738 |
| IMEI | 351579059262020 |
| Firmware | 5.3.0 |
| WAN transport | Ethernet (cellular `ppp0` disabled) |
| LAN | Ethernet `eth0`, DHCP client |
| MAC | `00:08:00:4A:04:23` |
| IPv4 | 192.168.1.45 / 255.255.255.0, gateway 192.168.1.1 |
| DNS | 192.168.1.1 |
| **LoRa mode** | **Network Server** — not packet forwarder, not Basic Station |

The LoRa mode matters: the Conduit terminates the LoRaWAN session itself, so
there is no upstream network server to register a device with. The join server
below is the whole story.

Both Conduits sit on the same subnet — this one at .45, the EU unit at .172 —
which is worth remembering when the TCP variant needs somewhere to send to.

### LoRa card

| | |
| --- | --- |
| Gateway EUI | `00-80-00-00-A0-00-2D-F0` |
| Frequency band | 915 |
| FPGA version | 31 |

### LoRaWAN network server

| setting | value |
| --- | --- |
| Channel plan | US915 |
| **Frequency sub-band** | **2** |
| Channel mask | empty (derived from the sub-band) |
| Network mode | Public LoRaWAN |
| NetID | `000000` |
| Join delay | 5 s |
| Rx1 delay | 1 s |
| Rx1 DR offset | 0 |
| Rx2 datarate | 8 — SF12BW500 |
| Min datarate | 0 — SF10BW125 |
| Max datarate | 4 — SF8BW500 |
| Tx power | 26 dBm |
| Antenna gain | 3 dBi |
| ADR step | 30 cB |
| ACK timeout | 5000 |
| Duty-cycle limit | disabled |
| Queue size | 16 |
| Address range | `00:00:00:01` – `FF:FF:FF:FE` |

### Join server

Location is **Local Keys**: the Conduit is its own join server, with no external
network server behind it.

`Local End-Device Credentials` is **empty**, and `Local Network Settings` is
enabled with a network-wide AppEUI and AppKey. In MultiTech's model that means
**any DevEUI presenting the right AppEUI/AppKey pair is accepted** — a
shared-key network.

| | |
| --- | --- |
| Network ID (AppEUI) | `0102030405060708` |
| Network Key (AppKey) | see "Credentials" |
| Default profile | DEFAULT-CLASS-A |

Both values look like MultiTech factory defaults — the AppEUI is
`01 02 … 08` and the key follows an equally regular pattern. If they have never
been changed, then every Conduit shipped with the same defaults shares them, and
the "shared root key" caveat below is not just about *your* units. Changing both
on the gateway and in the firmware config costs one build and closes that.

Two consequences worth stating plainly:

- **No per-unit provisioning.** A concentrator can be flashed and powered on
  without being registered first. The DevEUI only has to be unique, which is why
  deriving it from the nRF52840's factory ID (`CONFIG_HWINFO`, already enabled)
  is enough.
- **Every unit shares the root key.** Extracting the flash from one device
  compromises the whole network. Acceptable for a bench and a pilot; for a
  deployment, move to per-device entries under `Local End-Device Credentials`.

### What this decides in the firmware

| gateway setting | firmware consequence |
| --- | --- |
| sub-band 2 | channel mask `{0xFF00, 0, 0, 0, 0x0002, 0}` — channels 8-15 plus 65 — set with `lorawan_set_channels_mask()` **before** `lorawan_join()` |
| min datarate DR0 (SF10BW125) | confirms `CONSERVATIVE_MAX_PAYLOAD = 11` in `link_lora.cpp`: DR0 in US915 carries 11 bytes of application payload |
| max datarate DR4 (SF8BW500) | ceiling of 242 bytes, which is what `APP_LINK_TCP_MAX_FRAGMENT` mirrors so both link backends fragment alike |
| ADR step present | `lorawan_enable_adr(true)`. Zephyr recommends ADR only for devices in a mostly static location, which a room-mounted concentrator is |
| Class A profile | `lorawan_set_class(LORAWAN_CLASS_A)` |
| Public LoRaWAN | public sync word, the default — nothing to configure |
| join delay 5 s, Rx1 delay 1 s | LoRaMac-node's own US915 defaults; nothing to configure |
| Rx2 DR8 | the US915 standard; nothing to configure |

Getting the channel mask wrong is the classic US915 failure: the device hunts
all 72 channels and appears to fail for RF reasons it does not have.

## EU unit — 868 MHz

### Device

| | |
| --- | --- |
| Model | MTCDT-LEU1-246A |
| Serial | 19744816 |
| IMEI | 359852054355533 |
| Firmware | 5.3.0 |
| WAN transport | None (cellular `ppp0` disabled) |
| LAN | Ethernet `eth0`, DHCP client |
| MAC | `00:08:00:4A:56:4D` |
| IPv4 | 192.168.1.172 / 255.255.255.0 |
| DNS | 192.168.1.1 |

### Accessory card

| | |
| --- | --- |
| Slot | AP2 |
| Model | **MTAC-LORA-868** |
| Serial | 18468797 |
| Hardware | MTAC-LORA-1.0 |

### LoRaWAN network server

**Not captured yet.** The channel plan, join server mode and keys for this unit
are still to be read off its interface. Until then nothing here can be assumed
from the US unit: they are separate configurations on separate hardware.

### What EU868 would change in the firmware

This is not a variant the firmware supports today, and the differences are not
cosmetic:

- **Region.** `CONFIG_LORAWAN_REGION_EU868` instead of `US915`, selected through
  `APP_LINK_LORA_REGION_*`. Today that Kconfig offers US915 only.
- **No sub-bands.** EU868 has three mandatory join channels and a
  single-word channel mask (`LORAWAN_CHANNELS_MASK_SIZE_EU868 = 1`), against
  US915's six. The sub-band arithmetic above does not apply at all.
- **Duty cycle is enforced.** EU868 limits transmit time per band (1% on most),
  where US915 constrains dwell time instead. A 15-minute dispatch period that is
  comfortable in the US may not be legal in Europe once the payload grows —
  this needs checking against the real fragment count before anyone deploys.
- **Different payload table.** The per-datarate maximums differ, so
  `CONSERVATIVE_MAX_PAYLOAD` is a US915 number and would need its EU868
  equivalent.

If both regions are actually going to be deployed, region becomes a build
variant in the same way the transport already is: one Kconfig choice, one set of
constants behind it, and nothing above `hal/link` aware of which was picked.

## Credentials

The AppKey is deliberately absent from this file and from the repository.

Recommended handling: declare the key symbols in `Kconfig` with obviously fake
defaults, and override them from a `prj_local.conf` listed in `.gitignore`. That
keeps the build reproducible for anyone who clones the repo, keeps the real key
off GitHub, and is the same shape as flashing keys separately the day that
becomes necessary.

| | |
| --- | --- |
| US unit AppEUI | `0102030405060708` |
| US unit AppKey | known, 16 bytes, held locally — **not recorded here** |
| EU unit AppEUI | pending |
| EU unit AppKey | pending |
