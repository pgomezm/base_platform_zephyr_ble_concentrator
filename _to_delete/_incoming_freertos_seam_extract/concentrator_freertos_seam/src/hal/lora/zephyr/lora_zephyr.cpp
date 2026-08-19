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

LOG_MODULE_REGISTER(hal_lora, CONFIG_APP_LOG_LEVEL);

namespace hal::lora
{
namespace
{

/// The LoRa radio, from the `lora0` alias in the board overlay.
const struct device* const s_p_lora_device = DEVICE_DT_GET(DT_ALIAS(lora0));

/// Whether the network join has completed.
bool s_is_joined = false;

/// Conservative payload ceiling used before the network reports a data rate.
///
/// US915 DR0 carries very little; assuming a large payload before the data rate
/// is known would produce fragments the radio then refuses. See the payload
/// table in docs/ARCHITECTURE.md section 5, which is still flagged for
/// verification against the current regional parameters spec.
constexpr uint8_t k_conservative_max_payload = 11U;

} // namespace

Result initialize()
{
    if (!device_is_ready(s_p_lora_device))
    {
        LOG_ERR("LoRa device not ready; check the SPI wiring and the board overlay");
        return Result::NOT_READY;
    }

    const int result = lorawan_start();

    if (result != 0)
    {
        LOG_ERR("lorawan_start failed (%d)", result);
        return Result::CONFIG_ERROR;
    }

    LOG_INF("LoRa radio ready");
    return Result::OK;
}

Result join()
{
    // TODO(open item 2 in docs/ARCHITECTURE.md): OTAA vs ABP, and the
    // credentials, are still undecided. Both are a network-server question, not
    // a firmware one. Wiring in the join parameters is a small, contained change
    // here once that is settled; nothing above this layer is affected.
    LOG_WRN("LoRaWAN join not configured yet: no join mode or credentials decided");

    s_is_joined = false;
    return Result::JOIN_ERROR;
}

bool is_joined()
{
    return s_is_joined;
}

Result send(const uint8_t* p_data, size_t length)
{
    if (!device_is_ready(s_p_lora_device))
    {
        return Result::NOT_READY;
    }

    if (!s_is_joined)
    {
        return Result::JOIN_ERROR;
    }

    if (length > get_max_payload_size())
    {
        LOG_ERR("payload of %u bytes exceeds the %u byte limit at this data rate",
                static_cast<unsigned>(length), get_max_payload_size());
        return Result::PAYLOAD_TOO_LARGE;
    }

    const int result =
        lorawan_send(1U, const_cast<uint8_t*>(p_data), static_cast<uint8_t>(length),
                     LORAWAN_MSG_UNCONFIRMED);

    if (result != 0)
    {
        LOG_ERR("lorawan_send failed (%d)", result);
        return Result::SEND_ERROR;
    }

    return Result::OK;
}

uint8_t get_max_payload_size()
{
    if (!s_is_joined)
    {
        return 0U;
    }

    uint8_t max_size = 0U;
    uint8_t unused_size = 0U;

    lorawan_get_payload_sizes(&max_size, &unused_size);

    // A network that reports nothing gets the conservative floor rather than an
    // optimistic guess.
    return (max_size > 0U) ? max_size : k_conservative_max_payload;
}

} // namespace hal::lora

/// @}
