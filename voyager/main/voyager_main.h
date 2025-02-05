#include "nimble/ble.h"
#include "modlog/modlog.h"
#include "esp_log.h"
#include "esp_mac.h"

#define GATT_SVR_SVC_ENV_SENS_UUID 0x181A
#define GATT_SVR_SVC_ENV_SENS_CHR_HUMIDITY_UUID 0x2A6F

static char *voyager_tag = "voyager_ble";

struct voyager_app_context
{
    char *message;
};
