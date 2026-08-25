/// @addtogroup grp_hal_link
/// @{
///
/// @file link_lora.cpp
///
/// Source file that implements the Link HAL over Zephyr's LoRaWAN subsystem.

#include "hal/link/link.hpp"

#include "config.hpp"

#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan/lorawan.h>

#include <string.h>

LOG_MODULE_REGISTER(hal_link_lora, CONFIG_APP_LOG_LEVEL);

namespace hal::link
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

/// Length of a DevEUI, JoinEUI or any other 64-bit LoRaWAN identifier.
constexpr size_t EUI_SIZE = 8U;

/// Length of an AppKey.
constexpr size_t KEY_SIZE = 16U;

/// Parse a hex string into bytes.
///
/// Strict on purpose: a key that is one character short is a configuration
/// mistake that must fail loudly at bring-up, not silently produce a device
/// that cannot join for reasons nobody can see from the outside.
///
/// @param p_hex the input, exactly 2 * length characters
/// @param p_out where the bytes are written
/// @param length how many bytes are expected
/// @return true if the whole string was valid hex of the expected length
bool parse_hex(const char* p_hex, uint8_t* p_out, size_t length)
{
    if ((p_hex == nullptr) || (strlen(p_hex) != (length * 2U)))
    {
        return false;
    }

    for (size_t i = 0U; i < length; ++i)
    {
        uint8_t value = 0U;

        for (size_t nibble = 0U; nibble < 2U; ++nibble)
        {
            const char c = p_hex[(i * 2U) + nibble];
            uint8_t digit = 0U;

            if ((c >= '0') && (c <= '9'))
            {
                digit = static_cast<uint8_t>(c - '0');
            }
            else if ((c >= 'a') && (c <= 'f'))
            {
                digit = static_cast<uint8_t>((c - 'a') + 10);
            }
            else if ((c >= 'A') && (c <= 'F'))
            {
                digit = static_cast<uint8_t>((c - 'A') + 10);
            }
            else
            {
                return false;
            }

            value = static_cast<uint8_t>((value << 4) | digit);
        }

        p_out[i] = value;
    }

    return true;
}

/// Build the US915 channel mask for one sub-band.
///
/// US915 numbers 72 channels: 0-63 are 125 kHz and 64-71 are 500 kHz. Sub-band
/// n (1-8) owns the eight 125 kHz channels starting at (n-1)*8, plus the single
/// 500 kHz channel 63+n. The mask is six 16-bit words, the first four covering
/// the 125 kHz channels and the fifth the 500 kHz ones.
///
/// A gateway listens on one sub-band. Leaving all 72 enabled makes the device
/// hunt, which looks like an RF fault and is not one.
///
/// @param subband the sub-band, 1 to 8
/// @param p_mask where the six words are written
void build_us915_channel_mask(uint8_t subband, uint16_t* p_mask)
{
    for (size_t i = 0U; i < 6U; ++i)
    {
        p_mask[i] = 0U;
    }

    const uint8_t index = static_cast<uint8_t>(subband - 1U);

    // Eight consecutive 125 kHz channels, which never straddle a word boundary
    // because eight divides sixteen.
    p_mask[index / 2U] = static_cast<uint16_t>(0x00FFU << ((index % 2U) * 8U));

    // The one 500 kHz channel that comes with the sub-band.
    p_mask[4] = static_cast<uint16_t>(1U << index);
}

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
/// Given C language linkage because Zephyr calls it through a C function
/// pointer. On this target it would work either way — C and C++ share the
/// calling convention — but the standard does not promise that, and the marker
/// is where the C boundary is documented. Do not remove it as redundant.
extern "C" void downlink_callback(uint8_t port, uint8_t flags, int16_t rssi, int8_t snr,
                                  uint8_t length,
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

class LoraLink : public ILink
{
public:
    LoraLink() : m_is_joined(false) {}

    LinkError initialize() override
    {
        if (!device_is_ready(s_p_lora_device))
        {
            LOG_ERR("LoRa device not ready; check the SPI wiring and the board overlay");
            return LinkError::NOT_READY;
        }

        const int result = lorawan_start();

        if (result != 0)
        {
            LOG_ERR("lorawan_start failed (%d)", result);
            return LinkError::CONFIG_ERROR;
        }

        LOG_INF("LoRa radio ready");

        return LinkError::NO_ERROR;
    }

    LinkError connect() override
    {
        m_is_joined = false;

        uint8_t join_eui[EUI_SIZE];
        uint8_t app_key[KEY_SIZE];
        uint8_t dev_eui[EUI_SIZE];

        if (!parse_hex(config::LINK_LORA_JOIN_EUI, join_eui, sizeof(join_eui)))
        {
            LOG_ERR("JoinEUI is not %u hex characters", static_cast<unsigned>(EUI_SIZE * 2U));
            return LinkError::CONFIG_ERROR;
        }

        if (!parse_hex(config::LINK_LORA_APP_KEY, app_key, sizeof(app_key)))
        {
            LOG_ERR("AppKey is not %u hex characters", static_cast<unsigned>(KEY_SIZE * 2U));
            return LinkError::CONFIG_ERROR;
        }

        if (!resolve_dev_eui(dev_eui))
        {
            return LinkError::CONFIG_ERROR;
        }

        // The gateway listens on one sub-band. This must be set before the
        // join, not after.
        uint16_t channel_mask[LORAWAN_CHANNELS_MASK_SIZE_US915];
        build_us915_channel_mask(config::LINK_LORA_SUBBAND, channel_mask);

        if (lorawan_set_channels_mask(channel_mask, LORAWAN_CHANNELS_MASK_SIZE_US915) != 0)
        {
            LOG_ERR("lorawan_set_channels_mask failed for sub-band %u",
                    static_cast<unsigned>(config::LINK_LORA_SUBBAND));
            return LinkError::CONFIG_ERROR;
        }

        (void)lorawan_set_class(LORAWAN_CLASS_A);

        // The concentrator is mounted and does not move, which is the condition
        // Zephyr names for ADR being appropriate rather than harmful.
        lorawan_enable_adr(true);

        struct lorawan_join_config config = {};
        config.mode = LORAWAN_ACT_OTAA;
        config.dev_eui = dev_eui;
        config.otaa.join_eui = join_eui;
        config.otaa.app_key = app_key;
        config.otaa.nwk_key = app_key;

        // LoRaWAN 1.0.4 requires the DevNonce to increase across joins with the
        // same DevEUI. CONFIG_LORAWAN_NVM_SETTINGS is what carries it over a
        // reboot; without that the join server rejects the second boot as a
        // replay. Passing 0 here lets the stack use the stored value.
        config.otaa.dev_nonce = 0U;

        LOG_INF("joining: sub-band %u, DevEUI %02x%02x%02x%02x%02x%02x%02x%02x",
                static_cast<unsigned>(config::LINK_LORA_SUBBAND), dev_eui[0], dev_eui[1],
                dev_eui[2], dev_eui[3], dev_eui[4], dev_eui[5], dev_eui[6], dev_eui[7]);

        // Blocking, and a single attempt: Zephyr's stack does not retry a
        // failed join. Retrying is the state machine's job, through the
        // SOFT_ERROR path, which is why nothing here loops or backs off.
        const int result = lorawan_join(&config);

        if (result != 0)
        {
            LOG_ERR("lorawan_join failed (%d)", result);
            return LinkError::CONNECT_ERROR;
        }

        m_is_joined = true;
        LOG_INF("joined");

        return LinkError::NO_ERROR;
    }

    bool is_connected() const override
    {
        return m_is_joined;
    }

    LinkError send(const uint8_t* p_data, size_t length) override
    {
        if (!device_is_ready(s_p_lora_device))
        {
            return LinkError::NOT_READY;
        }

        if (!m_is_joined)
        {
            return LinkError::CONNECT_ERROR;
        }

        if (length > get_max_payload_size())
        {
            LOG_ERR("payload of %u bytes exceeds the %u byte limit at this data rate",
                    static_cast<unsigned>(length), get_max_payload_size());
            return LinkError::PAYLOAD_TOO_LARGE;
        }

        const int result = lorawan_send(1U, const_cast<uint8_t*>(p_data),
                                        static_cast<uint8_t>(length), LORAWAN_MSG_UNCONFIRMED);

        if (result != 0)
        {
            LOG_ERR("lorawan_send failed (%d)", result);
            return LinkError::SEND_ERROR;
        }

        return LinkError::NO_ERROR;
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

    uint8_t get_max_uplinks_per_dispatch() const override
    {
        return config::LINK_LORA_MAX_UPLINKS_PER_DISPATCH;
    }

private:
    /// Fill the DevEUI, either from the SoC or from Kconfig.
    ///
    /// @param p_dev_eui where the eight bytes are written
    /// @return true if a DevEUI was produced
    static bool resolve_dev_eui(uint8_t* p_dev_eui)
    {
#if defined(CONFIG_APP_LINK_LORA_DEV_EUI_FROM_HWINFO)
        // The nRF52840's factory identifier is eight bytes and unique per part,
        // which is exactly the shape of a DevEUI. Deriving it means a unit can
        // be flashed and powered on without being registered anywhere first.
        const ssize_t length = hwinfo_get_device_id(p_dev_eui, EUI_SIZE);

        if (length != static_cast<ssize_t>(EUI_SIZE))
        {
            LOG_ERR("hwinfo returned %d bytes, expected %u", static_cast<int>(length),
                    static_cast<unsigned>(EUI_SIZE));
            return false;
        }

        return true;
#else
        if (!parse_hex(config::LINK_LORA_DEV_EUI, p_dev_eui, EUI_SIZE))
        {
            LOG_ERR("DevEUI is not %u hex characters", static_cast<unsigned>(EUI_SIZE * 2U));
            return false;
        }

        return true;
#endif
    }

    bool m_is_joined;
    bool m_is_downlink_registered = false;
};

} // namespace

ILink& LinkFactory::get_instance()
{
    static LoraLink instance;

    return instance;
}

} // namespace hal::link

/// @}
