/* SPDX-License-Identifier: MIT */
#ifndef BRIDGE_XINPUT_REPORTS_H
#define BRIDGE_XINPUT_REPORTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { XINPUT_REPORT_BYTES = 20, DEMO_STEP_COUNT = 8 };
void demo_report(uint8_t out[XINPUT_REPORT_BYTES], unsigned step, uint8_t led_mode);
bool parse_player_led(const uint8_t *packet, size_t length, uint8_t *mode);

#endif
