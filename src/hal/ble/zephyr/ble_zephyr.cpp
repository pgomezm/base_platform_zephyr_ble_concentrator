/// @addtogroup grp_hal_ble
/// @{
///
/// @file ble_zephyr.cpp
///
/// Source file that implements the BLE HAL on Zephyr's Bluetooth stack.

#include "hal/ble/ble.hpp"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <string.h>

LOG_MODULE_REGISTER(hal_ble, CONFIG_APP_LOG_LEVEL);

namespace hal::ble
{
namespace
{

/// The registered report callback, or nullptr.
AdvReportCallback s_adv_report_callback = nullptr;

/// Whether scanning is currently active.
bool s_is_scanning = false;

/// Zephyr's scan callback. Runs in the Bluetooth RX thread.
///
/// Copies the report into a stack object and hands it to the registered
/// callback. Nothing here parses, blocks or allocates: see
/// docs/ARCHITECTURE.md section 4.
///
/// @param p_address advertiser address
/// @param rssi received signal strength, in dBm
/// @param adv_type advertising PDU type
/// @param p_buffer advertising payload
void scan_callback(const bt_addr_le_t* p_address, int8_t rssi, uint8_t adv_type,
                   struct net_buf_simple* p_buffer)
{
    ARG_UNUSED(adv_type);

    if (s_adv_report_callback == nullptr)
    {
        return;
    }

    AdvReport report{};

    static_assert(sizeof(report.address) == sizeof(p_address->a.val),
                  "BLE address size mismatch between HAL and Zephyr");
    memcpy(report.address, p_address->a.val, sizeof(report.address));

    report.address_type = p_address->type;
    report.rssi = rssi;

    const size_t length =
        (p_buffer->len > k_max_adv_data_size) ? k_max_adv_data_size : p_buffer->len;

    report.data_length = static_cast<uint8_t>(length);
    memcpy(report.data, p_buffer->data, length);

    s_adv_report_callback(report);
}

} // namespace

bool initialize()
{
    const int result = bt_enable(nullptr);

    if (result != 0)
    {
        LOG_ERR("bt_enable failed (%d)", result);
        return false;
    }

    LOG_INF("BLE subsystem ready");
    return true;
}

void register_adv_report_callback(AdvReportCallback callback)
{
    s_adv_report_callback = callback;
}

bool start_scan()
{
    if (s_is_scanning)
    {
        return true;
    }

    // Passive: the concentrator only listens. It never sends a scan request,
    // so it never transmits while collecting.
    struct bt_le_scan_param scan_parameters = {};
    scan_parameters.type = BT_LE_SCAN_TYPE_PASSIVE;
    scan_parameters.options = BT_LE_SCAN_OPT_NONE;
    scan_parameters.interval = BT_GAP_SCAN_FAST_INTERVAL;
    scan_parameters.window = BT_GAP_SCAN_FAST_WINDOW;

    const int result = bt_le_scan_start(&scan_parameters, scan_callback);

    if (result != 0)
    {
        LOG_ERR("bt_le_scan_start failed (%d)", result);
        return false;
    }

    s_is_scanning = true;
    LOG_INF("passive scan started");

    return true;
}

bool stop_scan()
{
    if (!s_is_scanning)
    {
        return true;
    }

    const int result = bt_le_scan_stop();

    if (result != 0)
    {
        LOG_ERR("bt_le_scan_stop failed (%d)", result);
        return false;
    }

    s_is_scanning = false;
    LOG_INF("passive scan stopped");

    return true;
}

bool is_scanning()
{
    return s_is_scanning;
}

} // namespace hal::ble

/// @}
