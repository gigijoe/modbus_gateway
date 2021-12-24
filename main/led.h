#ifndef _LED_H_
#define _LED_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"

typedef struct {
	gpio_num_t gpio;
	uint8_t tick;
	const uint8_t *pattern;
	size_t position;
	bool active;
	bool repeat;
} led;

void led_init(led *lc, gpio_num_t gpio);
void led_on(led *lc);
void led_off(led *lc);
void led_slow_flash(led *lc);
void led_normal_flash(led *lc);
void led_fast_flash(led *lc);
void led_sos_beep(led *lc);

void led_handler(led *lc);

extern led s_buzzer;
extern led s_led25;
extern led s_led27;

#ifdef __cplusplus
}
#endif

#endif
