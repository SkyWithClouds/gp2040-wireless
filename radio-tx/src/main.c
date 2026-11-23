/* SPDX-License-Identifier: MIT */
#include <zephyr/kernel.h>
#include "air.h"
#include "spi.h"
#include "transmitter.h"

struct demo_source {
	struct gp_input queue[GP_QUEUE_SIZE];
	uint16_t produced, released, last_buttons;
	uint32_t last_heartbeat, heartbeat;
};

/* Absolute time allows reconnection at the current step without replaying history. */
static uint16_t demo_buttons(uint32_t now, const struct gp_status *s)
{
	uint32_t phase = now % 8000;
	if (phase < 1000) return 0;
	if (phase < 3000) return 0x1000;
	if (phase < 4000) return 0x3000;
	if (phase < 5000) return 0;
	if (phase < 5004) return 0x1000;
	if (phase < 5008) return 0x3000;
	if (phase < 5012) return 0x2000;
	if (phase < 6000) return 0;
	if (phase < 7000) return 0x0008;
	if (!(s->flags & GP_LED_VALID)) return 0;
	uint8_t mode = s->led_mode;
	if (mode >= 2 && mode <= 5) return 0x1000u << (mode - 2);
	if (mode >= 6 && mode <= 9) return 0x1000u << (mode - 6);
	return 0;
}

static void demo_tick(struct demo_source *d, struct transmitter *t, uint32_t now)
{
	struct gp_frame f = {.session = t->session};
	uint16_t buttons = demo_buttons(now, &t->status);
	if (t->phase == OFFLINE) {
		*d = (struct demo_source){0};
		f.type = GP_HELLO;
		f.data.hello_nonce = air_nonce();
		tx_source(t, &f, now);
		return;
	}
	if (t->phase == OFFERED) {
		f.type = GP_SYNC;
		f.data.input.buttons = buttons;
		d->last_buttons = buttons;
		d->last_heartbeat = now;
		tx_source(t, &f, now);
	}
	if (t->phase < SYNCING) return;
	if (t->status.flags & GP_USB_DONE_VALID) d->released = t->status.usb_done;
	if (buttons != d->last_buttons) {
		if ((uint16_t)(d->produced - d->released) == GP_QUEUE_SIZE) {
			tx_reset(t, GP_OVERFLOW);
			return;
		}
		d->produced++;
		d->queue[d->produced % GP_QUEUE_SIZE] =
			(struct gp_input){.seq = d->produced, .buttons = buttons};
		d->last_buttons = buttons;
	}
	if (t->phase == ACTIVE && t->status.tx_accept != d->produced) {
		f.type = GP_INPUT;
		f.data.input = d->queue[(uint16_t)(t->status.tx_accept + 1) % GP_QUEUE_SIZE];
		tx_source(t, &f, now);
	}
	if (now - d->last_heartbeat >= GP_HEARTBEAT_MS) {
		d->last_heartbeat = now;
		f.type = GP_HEARTBEAT;
		/* No downstream Hitbox OUT in stage 2: never claim a physical LED apply. */
		f.data.heartbeat = (struct gp_heartbeat){.number = ++d->heartbeat};
		tx_source(t, &f, now);
	}
}

int main(void)
{
	static struct transmitter tx;
	static struct demo_source demo;
	bool external = false;
	tx_reset(&tx, GP_REBOOT);
	int rc = air_start(false);
	if (rc) return rc;
	rc = spi_init();
	if (rc) return rc;
	rc = spi_arm(&tx.reset);
	if (rc) return rc;
	for (;;) {
		uint32_t now = k_uptime_get_32();
		tx_tick(&tx, now);
		struct gp_frame frame;
		while (air_receive(&frame)) tx_ack(&tx, &frame, now);
		int transaction = spi_take(&frame);
		if (transaction > 0) {
			if (frame.type == GP_HELLO) external = true;
			if (external) tx_source(&tx, &frame, now);
		}
		if (!external) demo_tick(&demo, &tx, now);
		if (transaction) {
			struct gp_frame reply = tx_reply(&tx);
			rc = spi_arm(&reply);
			if (rc) return rc;
		}
		tx_schedule(&tx, k_ticks_to_us_floor32(k_uptime_ticks()), air_send);
		k_sleep(K_USEC(200));
	}
}
