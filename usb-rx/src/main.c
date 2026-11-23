/* SPDX-License-Identifier: MIT */
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usb_device.h>
#include <os_desc.h>
#include <usb_descriptor.h>

#include "reports.h"

enum { EP_IN = 0x81, EP_OUT = 0x02, MPS = 32, MS_REQUEST = 0x20 };

struct xinput_descriptors {
	struct usb_if_descriptor iface;
	uint8_t capabilities[17];
	struct usb_ep_descriptor in;
	struct usb_ep_descriptor out;
} __packed;

USBD_CLASS_DESCR_DEFINE(primary, 0) struct xinput_descriptors descriptors = {
	.iface = {
		.bLength = sizeof(struct usb_if_descriptor),
		.bDescriptorType = USB_DESC_INTERFACE,
		.bNumEndpoints = 2,
		.bInterfaceClass = 0xff, .bInterfaceSubClass = 0x5d, .bInterfaceProtocol = 1,
	},
	.capabilities = {
		17, 0x21, 0x00, 0x01, 0x01, 0x25, EP_IN, 20,
		0, 0, 0, 0, 0x13, EP_OUT, 8, 0, 0
	},
	.in = {
		.bLength = sizeof(struct usb_ep_descriptor), .bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = EP_IN, .bmAttributes = USB_DC_EP_INTERRUPT,
		.wMaxPacketSize = sys_cpu_to_le16(MPS), .bInterval = 1,
	},
	.out = {
		.bLength = sizeof(struct usb_ep_descriptor), .bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = EP_OUT, .bmAttributes = USB_DC_EP_INTERRUPT,
		.wMaxPacketSize = sys_cpu_to_le16(MPS), .bInterval = 8,
	},
};

static uint8_t ms_string[] = {
	18, USB_DESC_STRING, 'M', 0, 'S', 0, 'F', 0, 'T', 0, '1', 0, '0', 0, '0', 0,
	MS_REQUEST, 0,
};
static uint8_t compatible_id[40] = {
	40, 0, 0, 0, 0, 1, 4, 0, 1, /* length, version 1.00, index 4, one function */
	[16] = 0, 1, 'X', 'U', 'S', 'B', '1', '0', /* interface 0, Reserved1, ID */
};
static struct usb_os_descriptor os = {
	.string = ms_string, .string_len = sizeof(ms_string), .vendor_code = MS_REQUEST,
	.compat_id = compatible_id, .compat_id_len = sizeof(compatible_id),
};

static atomic_t configured;
static atomic_t suspended;
static atomic_t in_flight;
static atomic_t sequence_start;
/* One snapshot for the next integration stage: valid bit 24, generation 8..23, mode 0..7. */
static atomic_t player_led;
static uint8_t report[XINPUT_REPORT_BYTES];

static void on_out(uint8_t ep, enum usb_dc_ep_cb_status_code status)
{
	uint8_t packet[MPS], mode;
	uint32_t length;
	if (status != USB_DC_EP_DATA_OUT || usb_read(ep, packet, sizeof(packet), &length)) {
		return;
	}
	/* usb_read also re-arms OUT, including after rumble or malformed commands. */
	if (parse_player_led(packet, length, &mode)) {
		uint16_t gen = (uint16_t)(((uint32_t)atomic_get(&player_led) >> 8) + 1u);
		atomic_set(&player_led, BIT(24) | ((uint32_t)gen << 8) | mode);
	}
}

static void on_in(uint8_t ep, enum usb_dc_ep_cb_status_code status)
{
	ARG_UNUSED(ep);
	if (status == USB_DC_EP_DATA_IN) {
		atomic_clear(&in_flight);
	}
}

static void on_usb_status(enum usb_dc_status_code status, const uint8_t *param)
{
	switch (status) {
	case USB_DC_CONFIGURED:
		atomic_set(&sequence_start, k_uptime_get_32());
		atomic_clear(&in_flight);
		atomic_clear(&suspended);
		atomic_set(&configured, param && *param);
		break;
	case USB_DC_RESET:
	case USB_DC_DISCONNECTED:
		atomic_clear(&configured);
		atomic_clear(&in_flight);
		atomic_clear(&player_led);
		break;
	case USB_DC_SUSPEND: atomic_set(&suspended, 1); break;
	case USB_DC_RESUME: atomic_clear(&suspended); break;
	default: break;
	}
}

static struct usb_ep_cfg_data endpoints[] = {
	{ .ep_cb = on_in, .ep_addr = EP_IN },
	{ .ep_cb = on_out, .ep_addr = EP_OUT },
};

USBD_DEFINE_CFG_DATA(xinput) = {
	.interface_descriptor = &descriptors.iface,
	.num_endpoints = ARRAY_SIZE(endpoints),
	.endpoint = endpoints,
};

int main(void)
{
	/* The legacy core allocates the first free endpoint in each direction.
	 * It runs its fixup once. Restore the single-interface XUSB addresses in
	 * both tables before enabling USBD and registering endpoint callbacks. */
	if (!usb_get_device_descriptor()) {
		return -EINVAL;
	}
	descriptors.in.bEndpointAddress = EP_IN;
	endpoints[0].ep_addr = EP_IN;
	descriptors.out.bEndpointAddress = EP_OUT;
	endpoints[1].ep_addr = EP_OUT;
	usb_register_os_desc(&os);
	int err = usb_enable(on_usb_status);
	if (err) {
		return err;
	}

	for (;;) {
		if (atomic_get(&configured) && !atomic_get(&suspended) &&
		    atomic_cas(&in_flight, 0, 1)) {
			uint32_t elapsed = k_uptime_get_32() - (uint32_t)atomic_get(&sequence_start);
			demo_report(report, elapsed / 1000u, (uint8_t)atomic_get(&player_led));
			uint32_t written;
			if (usb_write(EP_IN, report, sizeof(report), &written) ||
			    written != sizeof(report)) {
				atomic_clear(&in_flight);
			}
		}
		k_sleep(K_MSEC(1));
	}
}
