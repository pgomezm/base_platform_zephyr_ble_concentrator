/// @addtogroup grp_hal_lora
/// @{
///
/// @file lora_zephyr.cpp
///
/// Source file that implements the LoRa HAL on Zephyr's LoRaWAN subsystem.

#include "hal/lora/lora.hpp"

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan/lorawan.h>

#include <string.h>

LOG_MODULE_REGISTER(hal_lora, CONFIG_APP_LOG_LEVEL);

namespace hal::lora
{
namespace
{

/// The LoRa radio, from the `lora0` alias in the board overlay.
const struct device* const s_p_lora_device = DEVICE_DT_GET(DT_ALIAS(lora0));

/// Conservative payload ceiling used before the network reports a data rate.
///
/// US915 DR0 carries very little; assuming a large payload before the data rate
/// is known would produce fragments the radio then refuses. See the payload
/// table in docs/ARCHITECTURE.md section 5, which is still flagged for
/// verification against the current regional parameters spec.
constexpr uint8_t CONSERVATIVE_MAX_PAYLOAD = 11U;

/// The registered downlink callback, or nullptr.
///
/// A file static rather than a member for the same reason hal::ble keeps its
/// report callback here: Zephyr's downlink callback is a plain function pointer
/// with no user-data slot.
DownlinkCallback s_downlink_callback = nullptr;

/// Zephyr's downlink callback. Runs in the LoRaWAN stack's thread.
///
/// Copies the payload into a stack object and hands it to the registered
/// callback. Nothing here parses, blocks or allocates.
void downlink_callback(uint8_t port, uint8_t flags, int16_t rssi, int8_t snr, uint8_t length,
                       const uint8_t* p_data)
{
    ARG_UNUSED(flags);

    if (s_downlink_callback == nullptr)
    {
        return;
    }

    Downlink downlink{};

    downlink.port = port;
    downlink.rssi = rssi;
    downlink.snr = snr;

    if (p_data != nullptr)
    {
        const size_t copied = (length > MAX_DOWNLINK_SIZE) ? MAX_DOWNLINK_SIZE : length;

        downlink.data_length = static_cast<uint8_t>(copied);
        memcpy(downlink.data, p_data, copied);
    }

    s_downlink_callback(downlink);
}

/// Registration record handed to Zephyr. Must outlive the registration, so it
/// is static rather than a local.
struct lorawan_downlink_cb s_downlink_registration = {
    .port = LW_RECV_PORT_ANY,
    .cb = downlink_callback,
};

class Lora : public ILora
{
public:
    Lora() : m_is_joined(false) {}

    LoraError initialize() override
    {
        if (!device_is_ready(s_p_lora_device))
        {
            LOG_ERR("LoRa device not ready; check the SPI wiring and the board overlay");
            return LoraError::NOT_READY;
        }

        const int result = lorawan_start();

        if (result != 0)
        {
            LOG_ERR("lorawan_start failed (%d)", result);
            return LoraError::CONFIG_ERROR;
        }

        LOG_INF("LoRa radio ready");

        return LoraError::NO_ERROR;
    }

    LoraError join() override
    {
        // TODO(open item 1 in docs/ARCHITECTURE.md): OTAA vs ABP, and the
        // credentials, are still undecided. Both are a network-server question,
        // not a firmware one. Wiring in the join parameters is a small,
        // contained change here once that is settled; nothing above this layer
        // is affected.
        LOG_WRN("LoRaWAN join not configured yet: no join mode or credentials decided");

        m_is_joined = false;

        return LoraError::JOIN_ERROR;
    }

    bool is_joined() const override
    {
        return m_is_joined;
    }

    LoraError send(const uint8_t* p_data, size_t length) override
    {
        if (!device_is_ready(s_p_lora_device))
        {
            return LoraError::NOT_READY;
        }

        if (!m_is_joined)
        {
            return LoraError::JOIN_ERROR;
        }

        if (length > get_max_payload_size())
        {
            LOG_ERR("payload of %u bytes exceeds the %u byte limit at this data rate",
                    static_cast<unsigned>(length), get_max_payload_size());
            return LoraError::PAYLOAD_TOO_LARGE;
        }

        const int result = lorawan_send(1U, const_cast<uint8_t*>(p_data),
                                        static_cast<uint8_t>(length), LORAWAN_MSG_UNCONFIRMED);

        if (result != 0)
        {
            LOG_ERR("lorawan_send failed (%d)", result);
            return LoraError::SEND_ERROR;
        }

        return LoraError::NO_ERROR;
    }

    void register_downlink_callback(DownlinkCallback callback) override
    {
        s_downlink_callback = callback;

        if (!m_is_downlink_registered)
        {
            lorawan_register_downlink_callback(&s_downlink_registration);
            m_is_downlink_registered = true;
        }
    }

    uint8_t get_max_payload_size() const override
    {
        if (!m_is_joined)
        {
            return 0U;
        }

        uint8_t max_size = 0U;
        uint8_t unused_size = 0U;

        lorawan_get_payload_sizes(&max_size, &unused_size);

        // A network that reports nothing gets the conservative floor rather
        // than an optimistic guess.
        return (max_size > 0U) ? max_size : CONSERVATIVE_MAX_PAYLOAD;
    }

private:
    bool m_is_joined;
    bool m_is_downlink_registered = false;
};

} // namespace

ILora& LoraFactory::get_instance()
{
    static Lora instance;

    return instance;
}

} // namespace hal::lora

/// @}
