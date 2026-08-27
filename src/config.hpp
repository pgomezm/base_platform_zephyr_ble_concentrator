/// @file config.hpp
///
/// Header file that declares the compile-time configuration of the firmware.
///
/// Every value here comes from Kconfig, so nothing in this file is edited to
/// change behaviour: edit `prj.conf` (or a board/variant conf) instead. The
/// defaults live in the top-level `Kconfig`.
///
/// This is the "config.h that says where it goes" from the original brief: the
/// destination of the uplink and the shape of the collection window are decided
/// here, at build time, and no module reads Kconfig symbols directly.

#pragma once

#include <cstdint>

// This file is a seam, like utils/log/log.hpp and hal/os/os.hpp: its whole job
// is to turn the build system's configuration symbols into constexpr values,
// so it is one of the few places allowed to name the build system.
//
// Narrowed to what it actually uses. It used to pull in the kernel and
// devicetree headers, which meant every module that read a configuration value
// got the whole kernel API in scope whether it wanted it or not - and that is
// how a rule like this quietly stops being true.
#include <zephyr/sys/util.h>

namespace config
{

/// Maximum number of distinct endpoint devices tracked at once.
///
/// The hard ceiling on how many sensors one concentrator can report from a
/// room. A new device arriving once the table is full evicts the least recently
/// seen one, and the eviction is counted.
constexpr uint16_t MAX_DEVICES = CONFIG_APP_MAX_DEVICES;

/// Depth of the pool holding raw advertising reports between the BLE callback
/// and the acquisition thread.
///
/// This is the burst tolerance: how many reports can arrive while the
/// acquisition thread is busy before one is dropped.
constexpr uint16_t ADV_REPORT_POOL_SIZE = CONFIG_APP_ADV_REPORT_POOL_SIZE;

/// Company identifier accepted in the Eddystone custom frame.
///
/// Advertising reports carrying any other company id are discarded in the
/// acquisition thread. This is the only content filter in the receive path.
constexpr uint16_t EXPECTED_COMPANY_ID = CONFIG_APP_EXPECTED_COMPANY_ID;

/// Period between uplink dispatches, in seconds.
///
/// The default comes from the transport: 900 s on LoRa, 30 s on TCP. See the
/// Kconfig help - the two products differ, the code does not.
constexpr uint32_t DISPATCH_PERIOD_S = CONFIG_APP_DISPATCH_PERIOD_S;

/// Period between uplink dispatches, in milliseconds.
constexpr uint32_t DISPATCH_PERIOD_MS = DISPATCH_PERIOD_S * 1000U;

/// Age after which a device that stopped advertising is considered stale, in
/// seconds.
///
/// A stale entry is not reported in the uplink and is the first candidate for
/// eviction when the table is full.
constexpr uint32_t DEVICE_STALE_AFTER_S = CONFIG_APP_DEVICE_STALE_AFTER_S;

/// Longest the device may go without sending anything, in seconds.
///
/// Seconds rather than dispatch cycles: what matters is how long the far end
/// may hear nothing, and that comes from whatever consumes the uplinks, not
/// from this firmware's dispatch period. Zero disables heartbeats. See the
/// Kconfig help for why silence is not a safe default.
constexpr uint32_t HEARTBEAT_MAX_SILENCE_S = CONFIG_APP_HEARTBEAT_MAX_SILENCE_S;

/// Identifier of this concentrator, carried in every uplink header.
constexpr uint32_t CONCENTRATOR_ID = CONFIG_APP_CONCENTRATOR_ID;

/// Whether the first uplink after boot is flagged as such.
///
/// Tells whatever consumes the uplinks that the device restarted and its device
/// table started empty, instead of leaving it to infer a gap.
constexpr bool REPORT_BOOT_IN_FIRST_UPLINK = IS_ENABLED(CONFIG_APP_REPORT_BOOT);

#if defined(CONFIG_APP_LINK_TCP)

/// IPv4 address of the uplink server, as a dotted quad.
constexpr const char* LINK_TCP_SERVER_ADDR = CONFIG_APP_LINK_TCP_SERVER_ADDR;

/// TCP port of the uplink server.
constexpr uint16_t LINK_TCP_SERVER_PORT = CONFIG_APP_LINK_TCP_SERVER_PORT;

/// How long a connection attempt may take before it is called a failure.
constexpr uint32_t LINK_TCP_CONNECT_TIMEOUT_MS = CONFIG_APP_LINK_TCP_CONNECT_TIMEOUT_MS;

/// Largest uplink fragment handed to the transport, in bytes.
///
/// Reported by hal::link::get_max_payload_size() so svc::comms fragments the
/// same way it does on LoRaWAN. See the Kconfig help for why a TCP link
/// declares a limit it does not have.
constexpr uint16_t LINK_TCP_MAX_FRAGMENT = CONFIG_APP_LINK_TCP_MAX_FRAGMENT;

#if !defined(CONFIG_APP_LINK_TCP_USE_DHCP)

/// Static IPv4 address of this concentrator, as a dotted quad.
constexpr const char* LINK_TCP_LOCAL_IP = CONFIG_APP_LINK_TCP_LOCAL_IP;

/// Static netmask, as a dotted quad.
constexpr const char* LINK_TCP_NETMASK = CONFIG_APP_LINK_TCP_NETMASK;

/// Static default gateway, as a dotted quad.
constexpr const char* LINK_TCP_GATEWAY = CONFIG_APP_LINK_TCP_GATEWAY;

#endif // !CONFIG_APP_LINK_TCP_USE_DHCP

#endif // CONFIG_APP_LINK_TCP

#if defined(CONFIG_APP_LINK_WIFI)

/// SSID of the access point to associate with.
constexpr const char* LINK_WIFI_SSID = CONFIG_APP_LINK_WIFI_SSID;

/// Pre-shared key of the access point, or an empty string for an open network.
///
/// A credential. The Kconfig default is deliberately empty: override it from a
/// gitignored prj_local.conf, the same place the LoRa keys live.
constexpr const char* LINK_WIFI_PSK = CONFIG_APP_LINK_WIFI_PSK;

/// IPv4 address of the uplink server, as a dotted quad.
constexpr const char* LINK_WIFI_SERVER_ADDR = CONFIG_APP_LINK_WIFI_SERVER_ADDR;

/// TCP port of the uplink server.
constexpr uint16_t LINK_WIFI_SERVER_PORT = CONFIG_APP_LINK_WIFI_SERVER_PORT;

/// How long a connection attempt may take before it is called a failure.
constexpr uint32_t LINK_WIFI_CONNECT_TIMEOUT_MS = CONFIG_APP_LINK_WIFI_CONNECT_TIMEOUT_MS;

/// Largest uplink fragment handed to the transport, in bytes.
constexpr uint16_t LINK_WIFI_MAX_FRAGMENT = CONFIG_APP_LINK_WIFI_MAX_FRAGMENT;

#if !defined(CONFIG_APP_LINK_WIFI_USE_DHCP)

/// Static IPv4 address of this concentrator, as a dotted quad.
constexpr const char* LINK_WIFI_LOCAL_IP = CONFIG_APP_LINK_WIFI_LOCAL_IP;

/// Static netmask, as a dotted quad.
constexpr const char* LINK_WIFI_NETMASK = CONFIG_APP_LINK_WIFI_NETMASK;

/// Static default gateway, as a dotted quad.
constexpr const char* LINK_WIFI_GATEWAY = CONFIG_APP_LINK_WIFI_GATEWAY;

#endif // !CONFIG_APP_LINK_WIFI_USE_DHCP

#endif // CONFIG_APP_LINK_WIFI

#if defined(CONFIG_APP_LINK_LORA)

/// US915 frequency sub-band the gateway listens on, 1 to 8.
constexpr uint8_t LINK_LORA_SUBBAND = CONFIG_APP_LINK_LORA_SUBBAND;

/// US915 data rate a session starts at, before ADR moves it.
///
/// Zero is the safe default. Raising it is a per-site decision that needs a
/// measured link; see the Kconfig help.
constexpr uint8_t LINK_LORA_INITIAL_DATARATE = CONFIG_APP_LINK_LORA_INITIAL_DATARATE;

/// Number of LoRaWAN transmissions one dispatch cycle may make.
///
/// Every uplink fragment is its own transmission, and several back to back is
/// airtime a single node is not entitled to. See the Kconfig help.
constexpr uint8_t LINK_LORA_MAX_UPLINKS_PER_DISPATCH =
    CONFIG_APP_LINK_LORA_MAX_UPLINKS_PER_DISPATCH;

/// JoinEUI / AppEUI, as 16 hex characters. Not a secret.
constexpr const char* LINK_LORA_JOIN_EUI = CONFIG_APP_LINK_LORA_JOIN_EUI;

/// AppKey, as 32 hex characters.
///
/// The value compiled in comes from Kconfig, whose default is deliberately not
/// a real key: override it from a gitignored prj_local.conf.
constexpr const char* LINK_LORA_APP_KEY = CONFIG_APP_LINK_LORA_APP_KEY;

/// Whether the DevEUI is derived from the SoC's factory identifier.
constexpr bool LINK_LORA_DEV_EUI_FROM_HWINFO = IS_ENABLED(CONFIG_APP_LINK_LORA_DEV_EUI_FROM_HWINFO);

#if !defined(CONFIG_APP_LINK_LORA_DEV_EUI_FROM_HWINFO)

/// DevEUI, as 16 hex characters, when it is not derived from hardware.
constexpr const char* LINK_LORA_DEV_EUI = CONFIG_APP_LINK_LORA_DEV_EUI;

#endif // !CONFIG_APP_LINK_LORA_DEV_EUI_FROM_HWINFO

#endif // CONFIG_APP_LINK_LORA

} // namespace config
