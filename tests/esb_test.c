/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "receiver.h"
#include "transmitter.h"

static struct gp_frame packet;
static uint64_t nonce(void) { static uint64_t id = 100; return ++id; }
static bool transmit(const struct gp_frame *f) { packet = *f; return true; }

static void session(void)
{
	struct transmitter tx = {0};
	struct receiver rx;
	receiver_reset(&rx);
	tx_reset(&tx, GP_REBOOT);
	struct gp_frame hello = {0, GP_HELLO, {.hello_nonce = 123}};
	tx_source(&tx, &hello, 0);
	assert(tx_schedule(&tx, 1000, transmit) && packet.type == GP_HELLO);
	struct gp_frame offer = receiver_frame(&rx, &packet, 1, 0, nonce);
	struct gp_frame repeated = receiver_frame(&rx, &packet, 1, 0, nonce);
	assert(offer.type == GP_OFFER && offer.session == repeated.session);
	tx_ack(&tx, &offer, 1);
	uint64_t id = tx.session;
	assert(id && tx_reply(&tx).type == GP_OFFER);
	struct gp_frame sync = {id, GP_SYNC, {.input = {.buttons = 0x1000}}};
	tx_source(&tx, &sync, 1);
	assert(tx_schedule(&tx, 1100, transmit) && packet.type == GP_SYNC);
	uint32_t lamp = (1u << 24) | (17u << 8) | 2;
	struct gp_frame ack = receiver_frame(&rx, &packet, 2, lamp, nonce);
	tx_ack(&tx, &ack, 2);
	assert(tx.phase == SYNCING && tx.status.led_gen == 17 && tx.status.led_mode == 2);
	assert(!(ack.data.status.flags & (GP_USB_DONE_VALID | GP_TX_ACCEPT_VALID)));
	assert(tx_schedule(&tx, 2500, transmit) && packet.type == GP_POLL);
	struct delivery d;
	assert(receiver_peek(&rx, &d) && !d.session);
	receiver_complete(&rx, &d, 3);
	assert(receiver_peek(&rx, &d) && d.session == id && d.input.buttons == 0x1000);
	receiver_complete(&rx, &d, 4);
	ack = receiver_frame(&rx, &packet, 4, lamp, nonce);
	tx_ack(&tx, &ack, 4);
	assert(tx.phase == ACTIVE && tx.status.flags == 15);
	assert(!tx_schedule(&tx, 4100, transmit));

	/* Fill the source window, drop #1 and an ACK; RX must reject all gaps. */
	struct gp_frame input = {id, GP_INPUT, {.input = {0}}};
	for (unsigned i = 1; i <= GP_WINDOW + 1; i++) {
		input.data.input.seq = i;
		input.data.input.buttons = i * 16;
		tx_source(&tx, &input, 5);
	}
	assert(tx.status.tx_accept == GP_WINDOW);
	for (unsigned i = 1; i <= GP_WINDOW; i++) {
		assert(tx_schedule(&tx, 5000 + i, transmit) && packet.data.input.seq == i);
		if (i > 1) {
			ack = receiver_frame(&rx, &packet, 5, lamp, nonce);
			assert(ack.data.status.rx_accept == 0);
		}
	}
	for (unsigned i = 1; i <= GP_WINDOW; i++) {
		assert(tx_schedule(&tx, 7000 + 2000 * i, transmit) && packet.data.input.seq == i);
		ack = receiver_frame(&rx, &packet, 7 + 2*i, lamp, nonce);
		if (i == 1) {
			/* Lost ACK: duplicate the RF packet at RX. The sender may already
			 * be replaying #2; its cumulative ACK must cover the missing ACK. */
			receiver_frame(&rx, &packet, 7 + 2*i, lamp, nonce);
			assert(rx.count == 1 && tx.status.rx_accept == 0);
			continue;
		}
		tx_ack(&tx, &ack, 7 + 2*i);
		assert(tx.status.rx_accept == i && tx.status.usb_done == 0);
	}
	assert(rx.count == GP_WINDOW && tx_schedule(&tx, 25000, transmit) && packet.type == GP_POLL);
	for (unsigned i = 1; i <= GP_WINDOW; i++) {
		assert(receiver_peek(&rx, &d) && d.input.seq == i && d.input.buttons == i * 16);
		receiver_complete(&rx, &d, 25);
	}
	ack = receiver_frame(&rx, &packet, 26, lamp, nonce);
	tx_ack(&tx, &ack, 26);
	assert(tx.status.usb_done == GP_WINDOW && !tx_schedule(&tx, 27000, transmit));
	input.data.input.seq = GP_WINDOW + 1;
	tx_source(&tx, &input, 27);
	assert(tx.status.tx_accept == GP_WINDOW + 1);

	/* Polls and repeated heartbeats cannot keep a dead input source alive. */
	struct gp_frame hb = {id, GP_HEARTBEAT, {.heartbeat = {.number = 7}}};
	receiver_frame(&rx, &hb, 30, lamp, nonce);
	receiver_frame(&rx, &hb, 79, lamp, nonce);
	assert(rx.session == id);
	receiver_tick(&rx, 80);
	assert(!rx.session && receiver_peek(&rx, &d) && !d.session && !d.input.buttons);
	ack = receiver_frame(&rx, &input, 81, lamp, nonce);
	assert(ack.type == GP_RESET);
	tx_ack(&tx, &ack, 81);
	assert(tx.phase == OFFLINE && !tx.status.flags);
}

static void wrap_and_stall(void)
{
	const uint64_t id = 3;
	struct transmitter tx = {.phase = ACTIVE, .session = id, .sync_sent = true,
		.status = {.flags = 7, .tx_accept = 65535, .rx_accept = 65535, .usb_done = 65535},
		.sent = 65535};
	struct receiver rx = {.session = id, .accepted = 65535, .done = 65535, .done_valid = true};
	struct gp_frame f = {id, GP_INPUT, {.input = {.seq = 0, .buttons = 0x3000}}};
	tx_source(&tx, &f, 1);
	assert(tx_schedule(&tx, 1000, transmit) && packet.type == GP_INPUT && !tx.sent);
	struct gp_frame ack = receiver_frame(&rx, &packet, 1, 0, nonce);
	tx_ack(&tx, &ack, 1);
	assert(!tx.status.rx_accept && tx.status.usb_done == 65535);
	ack.data.status.rx_accept = 1;
	tx_ack(&tx, &ack, 2);
	assert(!tx.status.rx_accept);
	struct delivery old;
	assert(receiver_peek(&rx, &old) && old.input.seq == 0);
	f = (struct gp_frame){id, GP_HEARTBEAT, {.heartbeat = {.number = 10}}};
	tx_source(&tx, &f, 49);
	ack = receiver_frame(&rx, &f, 49, 0, nonce);
	tx_ack(&tx, &ack, 49);
	tx_tick(&tx, 50);
	receiver_tick(&rx, 50);
	assert(tx.phase == ACTIVE && rx.session == id);
	tx_tick(&tx, 51);
	receiver_tick(&rx, 51);
	assert(tx.phase == OFFLINE && !rx.session && rx.neutral);
	/* Stale USB completion cannot advance a new SYNC with an identical seq. */
	struct gp_frame hello = {0, GP_HELLO, {.hello_nonce = 321}};
	struct gp_frame offer = receiver_frame(&rx, &hello, 52, 0, nonce);
	f = (struct gp_frame){offer.session, GP_SYNC, {.input = {.buttons = 8}}};
	receiver_frame(&rx, &f, 52, 0, nonce);
	receiver_complete(&rx, &old, 53);
	assert(rx.neutral && !rx.done_valid && rx.count == 1);
	struct delivery d;
	assert(receiver_peek(&rx, &d) && !d.session);
	receiver_complete(&rx, &d, 54);
	assert(receiver_peek(&rx, &d) && d.session == offer.session && d.input.buttons == 8);
	receiver_complete(&rx, &d, 55);
	assert(!rx.count && rx.done_valid);
	f = (struct gp_frame){id, GP_RESET, {.reset_reason = GP_REBOOT}};
	receiver_frame(&rx, &f, 56, 0, nonce);
	assert(rx.session == offer.session);
}

int main(void)
{
	session();
	wrap_and_stall();
	puts("ESB session, loss, ordering, window, LED, wrap, disconnect and stalled USB passed");
}
