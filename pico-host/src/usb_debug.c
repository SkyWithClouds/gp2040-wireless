/* SPDX-License-Identifier: MIT */
#include "tusb.h"
#include "pico/unique_id.h"

static const tusb_desc_device_t device = {
	.bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
	.bcdUSB = 0x0200, .bDeviceClass = TUSB_CLASS_MISC,
	.bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
	.bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE, .idVendor = 0x1209, .idProduct = 0x0002,
	.bcdDevice = 0x0100, .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3,
	.bNumConfigurations = 1,
};
uint8_t const *tud_descriptor_device_cb(void) { return (const uint8_t *)&device; }

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
	(void)index;
	static const uint8_t config[] = {
		TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN, 0, 500),
		TUD_CDC_DESCRIPTOR(0, 0, 0x81, 8, 0x02, 0x82, 64),
	};
	return config;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t language)
{
	(void)language;
	static uint16_t descriptor[32];
	static char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
	const char *strings[] = {NULL, "GP2040 Wireless", "Pico Hitbox Bridge", serial};
	if (index >= sizeof(strings) / sizeof(strings[0])) return NULL;
	unsigned length = 1;
	if (!index) {
		descriptor[1] = 0x0409;
	} else {
		if (index == 3) pico_get_unique_board_id_string(serial, sizeof(serial));
		length = strlen(strings[index]);
		for (unsigned i = 0; i < length; i++) descriptor[1+i] = strings[index][i];
	}
	descriptor[0] = (TUSB_DESC_STRING << 8) | (2 * length + 2);
	return descriptor;
}
