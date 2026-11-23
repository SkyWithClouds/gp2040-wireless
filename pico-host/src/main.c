/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include "hardware/clocks.h"
#include "hardware/spi.h"
#include "pico/rand.h"
#include "pico/stdlib.h"
#include "pio_usb.h"
#include "tusb.h"
#include "xinput_host.h"
#include "source.h"

enum { DM = 1, DP = 0, MISO = 16, CS = 17, SCK = 18, MOSI = 19, READY = 20 };
static struct source bridge;
static struct {
	uint8_t addr, instance;
	const xinputh_interface_t *itf;
	bool out_busy;
	uint64_t out_session;
	uint16_t out_gen;
	uint8_t out_mode;
	uint32_t retry_out, reports, led_success, bad_spi;
} pad;

usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *count)
{
	*count = 1;
	return &usbh_xinput_driver;
}

void tuh_xinput_mount_cb(uint8_t addr, uint8_t instance, const xinputh_interface_t *itf)
{
	if (pad.itf || itf->type != XBOX360_WIRED) return;
	pad.addr = addr;
	pad.instance = instance;
	pad.itf = itf;
	pad.retry_out = time_us_32();
}

void tuh_xinput_umount_cb(uint8_t addr, uint8_t instance)
{
	if (addr != pad.addr || instance != pad.instance) return;
	source_detach(&bridge, GP_HITBOX_REMOVED);
	pad.itf = NULL;
	pad.addr = 0;
	pad.out_busy = false;
}

void tuh_xinput_report_received_cb(uint8_t addr, uint8_t instance,
				   const xinputh_interface_t *itf, uint16_t interface_size)
{
	(void)interface_size; /* The driver supplies sizeof(*itf), not the USB byte count. */
	if (addr != pad.addr || instance != pad.instance) return;
	const uint8_t *p = itf->epin_buf;
	if (itf->last_xfer_result != XFER_RESULT_SUCCESS) {
		if (bridge.have_input) source_detach(&bridge, GP_USB_RESET);
	} else if (itf->last_xferred_bytes == 20 && !p[0] && p[1] == 20) {
		struct gp_input s = {.buttons = gp_read_le(p+2, 2), .lt = p[4], .rt = p[5],
			.lx = gp_read_axis(p+6), .ly = gp_read_axis(p+8),
			.rx = gp_read_axis(p+10), .ry = gp_read_axis(p+12)};
		source_input(&bridge, &s, time_us_32());
		pad.reports++;
	}
	tuh_xinput_receive_report(addr, instance);
}

void tuh_xinput_report_sent_cb(uint8_t addr, uint8_t instance, const uint8_t *report, uint16_t len)
{
	if (!pad.itf || !pad.out_busy || pad.addr != addr || pad.instance != instance) return;
	if (pad.itf->last_xfer_result == XFER_RESULT_SUCCESS && len == 3 &&
	    report[0] == 1 && report[1] == 3 && report[2] == pad.out_mode) {
		source_led_done(&bridge, pad.out_session, pad.out_gen);
		pad.led_success++;
	}
	pad.out_busy = false;
}

static void service_pad(uint32_t now)
{
	if (!pad.itf) return;
	if (!usbh_edpt_busy(pad.addr, pad.itf->ep_in))
		tuh_xinput_receive_report(pad.addr, pad.instance);
	if (!bridge.led_valid || pad.out_busy ||
	    (bridge.applied_valid && bridge.applied_gen == bridge.led_gen) ||
	    now - pad.retry_out < 10000) return;
	const uint8_t cmd[] = {1, 3, bridge.led_mode};
	pad.retry_out = now;
	if (tuh_xinput_send_report(pad.addr, pad.instance, cmd, sizeof(cmd))) {
		pad.out_busy = true;
		pad.out_session = bridge.session;
		pad.out_gen = bridge.led_gen;
		pad.out_mode = bridge.led_mode;
	}
}

static uint64_t nonce(void)
{
	uint64_t value;
	do { value = get_rand_64(); } while (!value);
	return value;
}

static void service_spi(uint32_t now)
{
	struct gp_frame f;
	uint8_t tx[GP_WIRE_SIZE], rx[GP_WIRE_SIZE];
	source_tick(&bridge, now);
	if (!gpio_get(READY) || !source_next(&bridge, now, nonce, &f)) return;
	if (!gp_encode(tx, sizeof(tx), &f)) panic("invalid SPI record");
	gpio_put(CS, 0);
	spi_write_read_blocking(spi0, tx, rx, sizeof(rx));
	while (spi_is_busy(spi0)) tight_loop_contents();
	gpio_put(CS, 1);
	if (gp_decode(&f, rx, sizeof(rx))) source_reply(&bridge, time_us_32(), &f);
	else pad.bad_spi++;
}

static void log_status(uint32_t now)
{
	static uint32_t last;
	if (now - last < 1000000 || !tud_cdc_connected()) return;
	last = now;
	char line[240];
	int len = snprintf(line, sizeof(line),
		"pad=%u active=%u reports=%lu changes=%lu seq=%u tx=%u rx=%u done=%u "
		"resets=%lu reason=%u spi_bad=%lu poll_late=%lu led_out=%lu mode=%u gen=%u applied=%u applied_gen=%u\r\n",
		pad.addr, bridge.active, (unsigned long)pad.reports, (unsigned long)bridge.changes,
		bridge.produced, bridge.accepted, bridge.rx_accept, bridge.done,
		(unsigned long)bridge.resets, bridge.last_reason, (unsigned long)pad.bad_spi,
		(unsigned long)bridge.overdue, (unsigned long)pad.led_success,
		bridge.led_mode, bridge.led_gen, bridge.applied_valid, bridge.applied_gen);
	if (len > 0 && (size_t)len < sizeof(line) && tud_cdc_write_available() >= (unsigned)len) {
		tud_cdc_write(line, len);
		tud_cdc_write_flush();
	}
}

int main(void)
{
	set_sys_clock_khz(120000, true);
	gpio_init(CS);
	gpio_put(CS, 1);
	gpio_set_dir(CS, GPIO_OUT);
	gpio_init(READY);
	gpio_pull_down(READY);
	spi_init(spi0, 1000000);
	spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
	gpio_set_function(SCK, GPIO_FUNC_SPI);
	gpio_set_function(MOSI, GPIO_FUNC_SPI);
	gpio_set_function(MISO, GPIO_FUNC_SPI);

	_Static_assert(DM == DP + 1, "PIO DPDM requires adjacent pins");
	pio_usb_configuration_t pio = PIO_USB_DEFAULT_CONFIG;
	pio.pin_dp = DP;
	pio.pinout = PIO_USB_PINOUT_DPDM;
	tusb_rhport_init_t device = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
	tusb_rhport_init_t host = {.role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_FULL};
	if (!tusb_init(0, &device) || !tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio) ||
	    !tusb_init(1, &host)) panic("USB init failed");
	for (;;) {
		tuh_task_ext(0, false);
		uint32_t now = time_us_32();
		service_spi(now);
		service_pad(now);
		tud_task_ext(0, false);
		log_status(now);
	}
}
