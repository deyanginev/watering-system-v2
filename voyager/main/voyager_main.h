#ifndef _VOYAGER_MAIN_h
#define _VOYAGER_MAIN_h
#include "nimble/ble.h"
#include "modlog/modlog.h"
#include "esp_log.h"
#include "esp_mac.h"

#define GATT_SVR_SVC_ENV_SENS_UUID 0x181A
#define GATT_SVR_SVC_ENV_SENS_CHR_HUMIDITY_UUID 0x2A6F

static char *voyager_tag = "voyager_ble";

enum voyager_module_ble_state
{
    VY_BLE_STATE_CONNECTING = 0,
    VY_BLE_STATE_CONNECTED = 1,
    VY_BLE_STATE_DISCONNECTED = 2,
};

struct voyager_module_ble_context
{
    voyager_module_ble_state state;
};

struct voyager_app_context
{
    struct voyager_module_ble_context ble;
    char *message;
};

#endif
