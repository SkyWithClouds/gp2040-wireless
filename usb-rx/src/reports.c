/* SPDX-License-Identifier: MIT */
#include "reports.h"
#include <string.h>

static void put16(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

bool parse_player_led(const uint8_t *packet, size_t length, uint8_t *mode)
{
	if (length < 3 || packet[0] != 0x01 || packet[1] != 0x03) {
		return false;
	}
	*mode = packet[2];
	return true;
}

void demo_report(uint8_t out[XINPUT_REPORT_BYTES], unsigned step, uint8_t led_mode)
{
	memset(out, 0, XINPUT_REPORT_BYTES);
	out[1] = XINPUT_REPORT_BYTES;
	switch (step % DEMO_STEP_COUNT) {
	case 1: put16(out + 2, 0x1000); break; /* A */
	case 2: put16(out + 2, 0x3000); break; /* A+B in one report */
	case 3: put16(out + 2, 0x0008); break; /* D-pad right */
	case 4: out[4] = 255; break;
	case 5: out[5] = 255; break;
	case 6:
		put16(out + 6, 0x6000);
		put16(out + 8, 0xa000);
		put16(out + 10, 0xa000);
		put16(out + 12, 0x6000);
		break;
	case 7: {
		/* Diagnostic phase: display player 1..4 as A, B, X or Y. */
		unsigned first;
		if (led_mode >= 2 && led_mode <= 5) {
			first = 2;
		} else if (led_mode >= 6 && led_mode <= 9) {
			first = 6;
		} else {
			break;
		}
		put16(out + 2, (uint16_t)(0x1000u << (led_mode - first)));
		break;
	}
	default: break; /* neutral */
	}
}
