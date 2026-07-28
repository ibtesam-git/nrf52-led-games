#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>
#include <stdbool.h>

#define NUM_LEDS             4
#define TICK_MS              20

#define HOLD_ALL_MS          3000   /* how long to hold all 4 to trigger game */
#define GET_READY_MS         1000   /* NEW: buffer time to let go of buttons */
#define ROUND_COMPLETE_MS    1000   /* NEW: pause after finishing a round */
#define LAST_LED_SHOW_MS     300    /* NEW: how long the last pressed LED stays lit */

#define BREATH_HALF_STEPS    30
#define PATTERN_FLASH_ON_MS  400
#define PATTERN_SHOW_GAP_MS  300
#define MAX_PATTERN          32
#define INPUT_TIMEOUT_MS     4000
#define GAMEOVER_FLASH_COUNT 6

static const struct pwm_dt_spec pwm_leds[NUM_LEDS] = {
    PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0)),
    PWM_DT_SPEC_GET(DT_ALIAS(pwm_led1)),
    PWM_DT_SPEC_GET(DT_ALIAS(pwm_led2)),
    PWM_DT_SPEC_GET(DT_ALIAS(pwm_led3)),
};

static const struct gpio_dt_spec buttons[NUM_LEDS] = {
    GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(sw2), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(sw3), gpios),
};

static struct gpio_callback button_cb_data[NUM_LEDS];
static volatile bool button_pressed[NUM_LEDS];
static volatile bool button_just_pressed[NUM_LEDS];

static void button_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    for (int i = 0; i < NUM_LEDS; i++) {
        if (pins & BIT(buttons[i].pin)) {
            bool now_pressed = gpio_pin_get_dt(&buttons[i]);
            button_pressed[i] = now_pressed;
            if (now_pressed) {
                button_just_pressed[i] = true;
            }
        }
    }
}

static void led_set(int i, bool on)
{
    pwm_set_pulse_dt(&pwm_leds[i], on ? pwm_leds[i].period : 0);
}

static void led_set_percent(int i, int percent)
{
    uint32_t pulse = (uint32_t)((uint64_t)pwm_leds[i].period * percent / 100);
    pwm_set_pulse_dt(&pwm_leds[i], pulse);
}

static void all_leds_off(void)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        led_set(i, false);
    }
}

static void all_leds_on(void)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        led_set(i, true);
    }
}

enum game_state {
    STATE_IDLE,
    STATE_GET_READY,       /* NEW */
    STATE_SHOW_PATTERN,
    STATE_WAIT_INPUT,
    STATE_ROUND_COMPLETE,  /* NEW */
    STATE_GAME_OVER,
};

static enum game_state state = STATE_IDLE;
static int breath_phase[NUM_LEDS];
static int hold_all_timer_ms;

static int get_ready_timer_ms;
static bool get_ready_led_on;

static uint8_t pattern[MAX_PATTERN];
static int pattern_length;
static int show_timer_ms;
static int input_index;
static int input_timeout_timer_ms;

static int round_complete_timer_ms;
static int last_correct_led = -1;

static int gameover_flash_count;
static int gameover_timer_ms;
static bool gameover_led_on;

static void start_new_game(void)
{
    pattern_length = 1;
    pattern[0] = sys_rand32_get() % NUM_LEDS;
    show_timer_ms = 0;
    all_leds_off();
    state = STATE_SHOW_PATTERN;
}

static void extend_pattern(void)
{
    pattern[pattern_length] = sys_rand32_get() % NUM_LEDS;
    pattern_length++;
    show_timer_ms = 0;
    all_leds_off();
    state = STATE_SHOW_PATTERN;
}

int main(void)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        if (!pwm_is_ready_dt(&pwm_leds[i])) {
            return 0;
        }
        led_set(i, false);
    }

    for (int i = 0; i < NUM_LEDS; i++) {
        if (!gpio_is_ready_dt(&buttons[i])) {
            return 0;
        }
        gpio_pin_configure_dt(&buttons[i], GPIO_INPUT);
        gpio_pin_interrupt_configure_dt(&buttons[i], GPIO_INT_EDGE_BOTH);
        gpio_init_callback(&button_cb_data[i], button_isr, BIT(buttons[i].pin));
        gpio_add_callback(buttons[i].port, &button_cb_data[i]);
    }

    while (1) {
        bool pressed[NUM_LEDS];
        bool just_pressed[NUM_LEDS];

        for (int i = 0; i < NUM_LEDS; i++) {
            pressed[i] = button_pressed[i];
            just_pressed[i] = button_just_pressed[i];
            button_just_pressed[i] = false;
        }

        switch (state) {

        case STATE_IDLE: {
            bool all_four = pressed[0] && pressed[1] && pressed[2] && pressed[3];

            if (all_four) {
                hold_all_timer_ms += TICK_MS;
                if (hold_all_timer_ms >= HOLD_ALL_MS) {
                    hold_all_timer_ms = 0;
                    get_ready_timer_ms = 0;
                    get_ready_led_on = false;
                    all_leds_off();
                    state = STATE_GET_READY;
                    break;
                }
            } else {
                hold_all_timer_ms = 0;
            }

            for (int i = 0; i < NUM_LEDS; i++) {
                if (pressed[i]) {
                    breath_phase[i] = (breath_phase[i] + 1) % (2 * BREATH_HALF_STEPS);
                    int p = breath_phase[i];
                    int percent = (p < BREATH_HALF_STEPS)
                        ? (p * 100 / BREATH_HALF_STEPS)
                        : ((2 * BREATH_HALF_STEPS - p) * 100 / BREATH_HALF_STEPS);
                    led_set_percent(i, percent);
                } else {
                    breath_phase[i] = 0;
                    led_set(i, false);
                }
            }
            break;
        }

        /* NEW: signal that the game is about to start, so you can let go */
        case STATE_GET_READY: {
            get_ready_timer_ms += TICK_MS;

            /* blink all 4 LEDs together fast, like a countdown flash */
            if ((get_ready_timer_ms / 150) % 2 == 0) {
                if (!get_ready_led_on) {
                    all_leds_on();
                    get_ready_led_on = true;
                }
            } else {
                if (get_ready_led_on) {
                    all_leds_off();
                    get_ready_led_on = false;
                }
            }

            if (get_ready_timer_ms >= GET_READY_MS) {
                all_leds_off();
                start_new_game();
            }
            break;
        }

        case STATE_SHOW_PATTERN: {
            show_timer_ms += TICK_MS;
            int cycle_len = PATTERN_FLASH_ON_MS + PATTERN_SHOW_GAP_MS;
            int step = show_timer_ms / cycle_len;
            int pos_in_step = show_timer_ms % cycle_len;

            if (step >= pattern_length) {
                all_leds_off();
                input_index = 0;
                input_timeout_timer_ms = 0;
                state = STATE_WAIT_INPUT;
                break;
            }

            int led_to_show = pattern[step];
            for (int i = 0; i < NUM_LEDS; i++) {
                led_set(i, (i == led_to_show) && (pos_in_step < PATTERN_FLASH_ON_MS));
            }
            break;
        }

        case STATE_WAIT_INPUT: {
            input_timeout_timer_ms += TICK_MS;

            int pressed_led = -1;
            for (int i = 0; i < NUM_LEDS; i++) {
                if (just_pressed[i]) {
                    pressed_led = i;
                }
            }

            if (pressed_led >= 0) {
                input_timeout_timer_ms = 0;
                led_set(pressed_led, true);

                if (pressed_led == pattern[input_index]) {
                    input_index++;

                    if (input_index >= pattern_length) {
                        /* NEW: don't jump straight to next round.
                         * Show the last correct LED, then pause. */
                        last_correct_led = pressed_led;
                        round_complete_timer_ms = 0;
                        state = STATE_ROUND_COMPLETE;
                    }
                } else {
                    all_leds_off();
                    gameover_flash_count = 0;
                    gameover_timer_ms = 0;
                    gameover_led_on = false;
                    state = STATE_GAME_OVER;
                }
            } else {
                for (int i = 0; i < NUM_LEDS; i++) {
                    if (!pressed[i]) {
                        led_set(i, false);
                    }
                }
                if (input_timeout_timer_ms >= INPUT_TIMEOUT_MS) {
                    all_leds_off();
                    gameover_flash_count = 0;
                    gameover_timer_ms = 0;
                    gameover_led_on = false;
                    state = STATE_GAME_OVER;
                }
            }
            break;
        }

        /* NEW: pause after a correct full round, shows last press, then waits */
        case STATE_ROUND_COMPLETE: {
            round_complete_timer_ms += TICK_MS;

            if (round_complete_timer_ms == LAST_LED_SHOW_MS) {
                all_leds_off();
            }

            if (round_complete_timer_ms >= ROUND_COMPLETE_MS) {
                extend_pattern();
            }
            break;
        }

        case STATE_GAME_OVER: {
            gameover_timer_ms += TICK_MS;
            if (gameover_timer_ms >= 150) {
                gameover_timer_ms = 0;
                gameover_led_on = !gameover_led_on;
                for (int i = 0; i < NUM_LEDS; i++) {
                    led_set(i, gameover_led_on);
                }
                if (!gameover_led_on) {
                    gameover_flash_count++;
                }
            }
            if (gameover_flash_count >= GAMEOVER_FLASH_COUNT) {
                all_leds_off();
                pattern_length = 0;
                state = STATE_IDLE;
            }
            break;
        }
        }

        k_msleep(TICK_MS);
    }

    return 0;
}