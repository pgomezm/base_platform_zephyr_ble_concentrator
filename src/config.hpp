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

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

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

/// Period between uplink dispatches, in minutes.
constexpr uint32_t DISPATCH_PERIOD_MIN = CONFIG_APP_DISPATCH_PERIOD_MIN;

/// Period between uplink dispatches, in milliseconds.
constexpr uint32_t DISPATCH_PERIOD_MS = DISPATCH_PERIOD_MIN * 60U * 1000U;

/// Age after which a device that stopped advertising is considered stale, in
/// seconds.
///
/// A stale entry is not reported in the uplink and is the first candidate for
/// eviction when the table is full.
constexpr uint32_t DEVICE_STALE_AFTER_S = CONFIG_APP_DEVICE_STALE_AFTER_S;

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

} // namespace config
