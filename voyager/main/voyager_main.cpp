/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "modules/actions/actions.h"
#include "voyager_ble/voyager_ble.h"
#include "voyager_main.h"
#include "host/util/util.h"

// Action *actions = (Action *)calloc(1, sizeof(Action));

static struct voyager_app_context _context =
    {.message = "test message"};

void app_main(void)
{

    ESP_LOGI(voyager_tag, "Initializing Nimble...");

    int rc = initialize(&_context);
    if (rc != ESP_OK)
    {
        ESP_LOGE(voyager_tag, "Could not initialize BLE: %d", rc);
    }
}
