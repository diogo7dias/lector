#pragma once
enum gpio_num_t : int {};
enum { GPIO_MODE_INPUT };
inline void gpio_set_direction(gpio_num_t, int) {}
inline void gpio_pullup_dis(gpio_num_t) {}
inline void gpio_pulldown_dis(gpio_num_t) {}
