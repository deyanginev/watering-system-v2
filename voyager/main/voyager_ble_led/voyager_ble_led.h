
#ifndef _VOYAGER_BLE_LED_h
#define _VOYAGER_BLE_LED_h
#include "modules/actions/actions.h"

void vy_ble_led_start(Action *a);
void vy_ble_led_tick(Action *a);
void vy_ble_led_stop(Action *a);

#endif