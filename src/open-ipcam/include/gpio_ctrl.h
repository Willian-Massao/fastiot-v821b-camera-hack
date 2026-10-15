#ifndef GPIO_CTRL_H
#define GPIO_CTRL_H

#include "common.h"

// GPIO Pin mappings for V821B camera
#define GPIO_BLE_EN       97
#define GPIO_KEY2         98
#define GPIO_RST_BTN      102
#define GPIO_IR_LED       103
#define GPIO_WHITE_LED    104
#define GPIO_IRCUT_ENA    118
#define GPIO_IRCUT_ENB    119
#define GPIO_SPK_EN       79

int gpio_init_all(void);
void gpio_cleanup_all(void);

int gpio_export(int gpio);
int gpio_set_direction(int gpio, const char *dir);
int gpio_set_value(int gpio, int val);
int gpio_get_value(int gpio);

// High-level camera peripheral controls
int set_night_mode(night_mode_t mode);
int set_ircut_filter(bool day_filter_on);
int set_ir_led(bool state);
int set_white_led(int brightness);
int set_siren(bool state);

#endif // GPIO_CTRL_H
