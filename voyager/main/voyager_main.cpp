/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "modules/actions/actions.h"
#include "voyager_ble/voyager_ble.h"
#include "voyager_ble_led/voyager_ble_led.h"
#include "voyager_main.h"
#include "host/util/util.h"
#include "nvs_flash.h"

static struct voyager_app_context _context = {
    .ble = {
        .state = VY_BLE_STATE_DISCONNECTED,
    },
    .message = "test message",
};

const int ACTIONS_COUNT = 1;

static const int VY_BLE_ACTION_INDEX = 0;
static const int VY_LED_ACTION_INDEX = 1;

// An array used for iterating over scheduled
// actions and executing them
Action availableActions[] = {
    {
        .frozen = false,
        .name = "ble",
        .ti = 1,
        .td = 0,
        .lst = 0,
        .to = 0,
        .state = MS_NON_ACTIVE,
        .tick = &vy_ble_tick,
        .start = &vy_ble_start,
        .stop = &vy_ble_stop,
        .context = &_context,
        .child = nullptr,
    },
    {
        .frozen = false,
        .name = "ble_led",
        .ti = 1,
        .td = 0,
        .lst = 0,
        .to = 0,
        .state = MS_NON_ACTIVE,
        .tick = &vy_ble_led_tick,
        .start = &vy_ble_led_start,
        .stop = &vy_ble_led_stop,
        .context = &_context,
        .child = nullptr,
    },
};

ActionsList executionList;

static void setup_nvs_flash()
{
    // init flash memory

    ESP_LOGI(voyager_tag, "Initializing NVS flash...");
    int rc = nvs_flash_init();
    while (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        rc = nvs_flash_init();
    }

    ESP_LOGI(voyager_tag, "Initialized NVS flash");
}

extern "C" void app_main(void)
{

    setup_nvs_flash();

    initActionsList(ACTIONS_COUNT);
    scheduleAction(&executionList, &availableActions[VY_BLE_ACTION_INDEX]);
    scheduleAction(&executionList, &availableActions[VY_LED_ACTION_INDEX]);

    while (true)
    {
        doQueueActions(&executionList, esp_log_timestamp());
    }

    ESP_LOGI(voyager_tag, "Initialized Nimble...");
}
