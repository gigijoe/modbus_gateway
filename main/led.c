#include "led.h"

led s_buzzer = {0};
led s_led25 = {0};
led s_led27 = {0};

#define LED_COMMAND_REPEAT 0xFE
#define LED_COMMAND_STOP   0xFF

static const uint8_t pattern_slow_flash[] = {
    50, 50, LED_COMMAND_STOP
};

static const uint8_t pattern_normal_flash[] = {
    25, 25, LED_COMMAND_STOP
};

static const uint8_t pattern_fast_flash[] = {
    2, 2, LED_COMMAND_STOP
};

// SOS morse code:
static const uint8_t pattern_sos_beep[] = {
    10, 10, 10, 10, 10, 40, 40, 10, 40, 10, 40, 40, 10, 10, 10, 10, 10, 70, LED_COMMAND_STOP
};


void led_init(led *lc, gpio_num_t gpio)
{
	lc->gpio = gpio;

    gpio_pad_select_gpio(lc->gpio);
    gpio_set_direction(lc->gpio, GPIO_MODE_OUTPUT);	
	gpio_set_level(lc->gpio, 0);
}

void led_on(led *lc)
{
	lc->tick = 0;
	lc->position = 0;
	lc->active = true;	
	lc->repeat = false;
	lc->pattern = 0;
	gpio_set_level(lc->gpio, 1); /* led on */
}

void led_off(led *lc)
{
	lc->tick = 0;
	lc->position = 0;
	lc->active = false;	
	lc->repeat = false;
	lc->pattern = 0;
	gpio_set_level(lc->gpio, 0); /* led off */
}

static void led_pattern(led *lc, const uint8_t *p)
{
	if(lc->pattern == p)
		return;
	
	lc->tick = 0;
	lc->position = 0;
	lc->active = true;	
	lc->repeat = true;
	lc->pattern = p;
	if(lc->pattern && lc->pattern[0] != 0)
		gpio_set_level(lc->gpio, 1); /* led on */
	else
		gpio_set_level(lc->gpio, 0); /* led off */
}

void led_slow_flash(led *lc)
{
	led_pattern(lc, pattern_slow_flash);
}

void led_normal_flash(led *lc)
{
	led_pattern(lc, pattern_normal_flash);
}

void led_fast_flash(led *lc)
{
	led_pattern(lc, pattern_fast_flash);
}

void led_sos_beep(led *lc)
{
	led_pattern(lc, pattern_sos_beep);
}

void led_handler(led *lc)
{
	if(lc->pattern == 0)
		return;

	if(lc->active == false)
		return;

	uint8_t period = lc->pattern[lc->position];

	if(++lc->tick > period) {
		lc->position++;
		lc->tick = 0;
		if(lc->pattern[lc->position] == LED_COMMAND_STOP) {
			gpio_set_level(lc->gpio, 0); /* led off */
			if(lc->repeat) {
				lc->tick = 0;
				lc->position = 0;
			} else
				lc->active = false;
			return;
		}
	}

	if(lc->pattern[lc->position] == 0)
		return; /* zero time */

	if(lc->position % 2 == 0)
		gpio_set_level(lc->gpio, 1); /* led on */
	else 
		gpio_set_level(lc->gpio, 0); /* led off */
}

