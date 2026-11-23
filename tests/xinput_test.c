#include <assert.h>
#include <string.h>
#include "reports.h"

int main(void)
{
	uint8_t mode = 0x55;
	uint8_t command[] = {1, 3, 9};
	for (size_t n = 0; n < 3; ++n) {
		assert(!parse_player_led(command, n, &mode) && mode == 0x55);
	}
	assert(parse_player_led(command, sizeof(command), &mode) && mode == 9);
	command[0] = 0;
	assert(!parse_player_led(command, sizeof(command), &mode) && mode == 9);
	command[0] = 1;
	command[1] = 8;
	assert(!parse_player_led(command, sizeof(command), &mode));
	const uint8_t expect[][20] = {
		{0,20}, {0,20,0,0x10}, {0,20,0,0x30}, {0,20,8},
		{0,20,0,0,255}, {0,20,0,0,0,255},
		{0,20,0,0,0,0,0,0x60,0,0xa0,0,0xa0,0,0x60}, {0,20,0,0x80}
	};
	uint8_t report[20];
	for (unsigned i = 0; i < DEMO_STEP_COUNT * 2; ++i) {
		demo_report(report, i, mode);
		assert(!memcmp(report, expect[i % DEMO_STEP_COUNT], sizeof(report)));
	}
	for (uint8_t m = 0; m <= 10; ++m) {
		demo_report(report, 7, m);
		unsigned button = m >= 2 && m <= 9 ? (0x10u << ((m - 2) % 4)) : 0;
		assert(report[3] == button);
	}
}
