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
/// Given C language linkage because Zephyr calls it through a C function
/// pointer. On this target it would work either way — C and C++ share the
/// calling convention — but the standard does not promise that, and the marker
/// is where the C boundary is documented. Do not remove it as redundant.
extern "C" void scan_callback(const bt_addr_le_t* p_address, int8_t rssi, uint8_t adv_type,
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
        (p_buffer->len > MAX_ADV_DATA_SIZE) ? MAX_ADV_DATA_SIZE : p_buffer->len;

    report.data_length = static_cast<uint8_t>(length);
    memcpy(report.data, p_buffer->data, length);

    s_adv_report_callback(report);
}

class Ble : public IBle
{
public:
    Ble() : m_is_initialized(false) {}

    BleError initialize() override
    {
        if (m_is_initialized)
        {
            return BleError::ALREADY_RUNNING;
        }

        const int result = bt_enable(nullptr);

        if (result != 0)
        {
            LOG_ERR("bt_enable failed (%d)", result);
            return BleError::HARDWARE_ERROR;
        }

        m_is_initialized = true;
        LOG_INF("BLE subsystem ready");

        return BleError::NO_ERROR;
    }

    void register_adv_report_callback(AdvReportCallback callback) override
    {
        // Kept in a file static rather than a member because Zephyr's scan
        // callback is a plain function pointer with no user-data slot, so
        // scan_callback() above has no way to reach an instance.
        s_adv_report_callback = callback;
    }

    BleError start_scan() override
    {
        if (s_is_scanning)
        {
            return BleError::NO_ERROR;
        }

        // Passive: the concentrator only listens. It never sends a scan
        // request, so it never transmits while collecting.
        struct bt_le_scan_param scan_parameters = {};
        scan_parameters.type = BT_LE_SCAN_TYPE_PASSIVE;
        scan_parameters.options = BT_LE_SCAN_OPT_NONE;
        scan_parameters.interval = BT_GAP_SCAN_FAST_INTERVAL;
        scan_parameters.window = BT_GAP_SCAN_FAST_WINDOW;

        const int result = bt_le_scan_start(&scan_parameters, scan_callback);

        if (result != 0)
        {
            LOG_ERR("bt_le_scan_start failed (%d)", result);
            return BleError::HARDWARE_ERROR;
        }

        s_is_scanning = true;
        LOG_INF("passive scan started");

        return BleError::NO_ERROR;
    }

    BleError stop_scan() override
    {
        if (!s_is_scanning)
        {
            return BleError::NO_ERROR;
        }

        const int result = bt_le_scan_stop();

        if (result != 0)
        {
            LOG_ERR("bt_le_scan_stop failed (%d)", result);
            return BleError::HARDWARE_ERROR;
        }

        s_is_scanning = false;
        LOG_INF("passive scan stopped");

        return BleError::NO_ERROR;
    }

    bool is_scanning() const override
    {
        return s_is_scanning;
    }

private:
    bool m_is_initialized;
};

} // namespace

IBle& BleFactory::get_instance()
{
    static Ble instance;

    return instance;
}

} // namespace hal::ble

/// @}
