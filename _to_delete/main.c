/*
 * Bring-up smoke test for base_platform_zephyr_ble_concentrator.
 *
 * Proves two things on real hardware, nothing more:
 *   1. BLE passive scanning works (native nRF52840 radio) — logs every
 *      advertising report it sees.
 *   2. The inAir9/SX1276 module answers over SPI and can transmit — sends a
 *      short raw LoRa packet every 10 seconds and logs the result.
 *
 * This is NOT the real application. It's plain C, single file, no hal/svc/eda
 * structure — replaced once both of the above are proven, by the real
 * app/hal/svc/eda tree from docs/ARCHITECTURE.md. Cross-check every Zephyr
 * API used here against whatever revision actually ends up pinned in
 * west.yml — this was written against the v4.4.1 API shape from research,
 * not built and tested against real hardware yet.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/drivers/lora.h>

LOG_MODULE_REGISTER(bringup, LOG_LEVEL_INF);

/* ---- BLE: passive scan, log everything we see ---- */

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
		     struct net_buf_simple *buf)
{
	char addr_str[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	LOG_INF("ADV: addr=%s rssi=%d type=%u len=%u", addr_str, rssi, adv_type, buf->len);
}

static int start_ble_scan(void)
{
	int err = bt_enable(NULL);

	if (err) {
		LOG_ERR("bt_enable failed (%d)", err);
		return err;
	}

	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	err = bt_le_scan_start(&scan_param, scan_cb);
	if (err) {
		LOG_ERR("bt_le_scan_start failed (%d)", err);
		return err;
	}

	LOG_INF("BLE passive scan started");
	return 0;
}

/* ---- LoRa: raw PHY send every 10s, log tx result ---- */

#define LORA_TEST_FREQUENCY_HZ 903900000 /* US915 channel — pick a real one for your plan before real use */
#define LORA_TX_INTERVAL_S 10

static int start_lora_test_tx(void)
{
	const struct device *lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));

	if (!device_is_ready(lora_dev)) {
		LOG_ERR("LoRa device not ready — check the overlay wiring against your actual board");
		return -ENODEV;
	}

	struct lora_modem_config config = {
		.frequency = LORA_TEST_FREQUENCY_HZ,
		.bandwidth = BW_125_KHZ,
		.datarate = SF_7,
		.coding_rate = CR_4_5,
		.preamble_len = 8,
		.tx_power = 14,
		.tx = true,
	};

	int err = lora_config(lora_dev, &config);

	if (err) {
		LOG_ERR("lora_config failed (%d) — SPI/reset wiring is the first thing to check", err);
		return err;
	}

	LOG_INF("LoRa radio configured, starting periodic test TX");

	static uint32_t seq;

	while (1) {
		char payload[16];
		int len = snprintf(payload, sizeof(payload), "bringup-%u", seq++);

		int ret = lora_send(lora_dev, (uint8_t *)payload, len);

		if (ret < 0) {
			LOG_ERR("lora_send failed (%d)", ret);
		} else {
			LOG_INF("LoRa TX ok: \"%s\"", payload);
		}

		k_sleep(K_SECONDS(LORA_TX_INTERVAL_S));
	}

	return 0;
}

int main(void)
{
	LOG_INF("base_platform_zephyr_ble_concentrator bring-up smoke test starting");

	if (start_ble_scan() != 0) {
		LOG_ERR("BLE scan did not start — see error above");
	}

	/* Runs forever, sending on the LoRa radio; BLE scan callback keeps
	 * firing in the background from the BT RX thread while this loop
	 * blocks in this thread. That's deliberate for the smoke test; the
	 * real app never blocks the LoRa TX path from application code
	 * outside svc/comms, per docs/ARCHITECTURE.md's execution-context
	 * rules. */
	start_lora_test_tx();

	return 0;
}
