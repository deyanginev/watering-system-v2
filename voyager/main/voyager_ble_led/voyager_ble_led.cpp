#include <stdio.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "sdkconfig.h"

#include "voyager_ble_led.h"
#include "voyager_main.h"
#include "voyager_ble/voyager_ble.h"

static const int BLINK_GPIO = 10;

static led_strip_handle_t led_strip;

static void configure_led(void)
{
    ESP_LOGI(voyager_tag, "Example configured to blink addressable LED!");
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // at least one LED on board
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip);
}

void vy_ble_led_start(Action *a)
{
    configure_led();
}
void vy_ble_led_tick(Action *a)
{
    voyager_app_context *context = (voyager_app_context *)a->context;
    if (context->ble.state == VY_BLE_STATE_CONNECTED)
    {
        led_strip_set_pixel(led_strip, 0, 0, 50, 0);
    }
    else if (context->ble.state == VY_BLE_STATE_CONNECTING)
    {
        led_strip_set_pixel(led_strip, 0, 50, 50, 0);
    }
    else if (context->ble.state == VY_BLE_STATE_DISCONNECTED)
    {
        led_strip_set_pixel(led_strip, 0, 50, 0, 0);
    }
    /* Refresh the strip to send data */
    led_strip_refresh(led_strip);
}
void vy_ble_led_stop(Action *a)
{
    led_strip_clear(led_strip);
}