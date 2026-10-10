#ifndef APPLICATION_LED_PWM_H
#define APPLICATION_LED_PWM_H

#include <stdint.h>

#define LED_PWM_MAX_DUTY 1000U

void led_pwm_init(void);
/* duty is in tenths of a percent and must be <= LED_PWM_MAX_DUTY. */
void led_pwm_set_duty(uint16_t duty);
void led_pwm_deinit(void);

#endif
