#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "host/ble_uuid.h"
#include "voyager_main.h"

extern char *voyager_tag;

// static const ble_uuid128_t gatt_svr_svc_uuid =
//     BLE_UUID128_INIT(0x2d, 0x71, 0xa2, 0x59, 0xb4, 0x58, 0xc8, 0x12,
//                      0x99, 0x99, 0x43, 0x95, 0x12, 0x2f, 0x46, 0x59);

// static const ble_uuid128_t gatt_svr_svr_chr_uuid =
//     BLE_UUID128_INIT(0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0x11, 0x11,
//                      0x22, 0x22, 0x22, 0x22, 0x33, 0x33, 0x33, 0x33);

static int voyager_ble_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const ble_uuid_t *uuid;
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
    {
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE)
        {
            MODLOG_DFLT(INFO, "Characteristic read; conn_handle=%d attr_handle=%d", conn_handle, attr_handle);
        }
        else
        {
            MODLOG_DFLT(INFO, "Characteristic read by Nimble stack; attr_handle=%d", attr_handle);
        }
        uuid = ctxt->chr->uuid;

        const ble_uuid16_t ownUuid = {
            .u = {
                .type = BLE_UUID_TYPE_16,
            },
            .value = GATT_SVR_SVC_ENV_SENS_CHR_HUMIDITY_UUID};

        if (ble_uuid_cmp(uuid, (ble_uuid_t *)&ownUuid) == 0)
        {
            struct voyager_app_context *voyager_app = (struct voyager_app_context *)arg;

            MODLOG_DFLT(INFO, "Appending string: %s", voyager_app->message);
            int rc = os_mbuf_append(ctxt->om, voyager_app->message, sizeof(char) * strlen(voyager_app->message));

            if (rc != ESP_OK)
            {
                MODLOG_ERROR(ERROR, "Error appending attribute value: %d", rc);
            }
            return 0;
        }
    }
    break;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static const ble_uuid16_t humidityChUuid = {
    .u = {
        .type = BLE_UUID_TYPE_16,
    },
    .value = GATT_SVR_SVC_ENV_SENS_CHR_HUMIDITY_UUID};

static ble_gatt_chr_def ble_gatt_chr_defs[] = {
    {.uuid = (ble_uuid_t *)&humidityChUuid,
     .access_cb = voyager_ble_access,
     .flags = BLE_GATT_CHR_F_READ | BLE_ATT_F_READ_ENC},
    {
        0,
    }};

static const ble_uuid16_t humidityEnvSensSvcUuid = {
    .u = {
        .type = BLE_UUID_TYPE_16,
    },
    .value = GATT_SVR_SVC_ENV_SENS_UUID};

static ble_gatt_svc_def voyager_ble_svc_defs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = (ble_uuid_t *)&humidityEnvSensSvcUuid,
     .characteristics = ble_gatt_chr_defs},
    {
        0,
    }};

int initialize_voyager_primary(void *ctx)
{
    ESP_LOGI(voyager_tag, "Registering services...");

    struct ble_gatt_svc_def *curr = &voyager_ble_svc_defs[0];
    int service_index = 0;
    while (curr->characteristics != 0)
    {
        int chr_index = 0;
        struct ble_gatt_chr_def *ch = (struct ble_gatt_chr_def *)&curr->characteristics[0];
        while (ch->access_cb != 0)
        {
            ch->arg = ctx;
            ch = (struct ble_gatt_chr_def *)&curr->characteristics[++chr_index];
        }
        curr = &voyager_ble_svc_defs[++service_index];
    }

    int rc;
    rc = ble_gatts_count_cfg(voyager_ble_svc_defs);
    if (rc != ESP_OK)
    {
        ESP_LOGE(voyager_tag, "Error while counting GATTs: %d", rc);
        return rc;
    }

    rc = ble_gatts_add_svcs(voyager_ble_svc_defs);

    if (rc != ESP_OK)
    {
        ESP_LOGE(voyager_tag, "Error while counting GATTs: %d", rc);
        return rc;
    }

    return ESP_OK;
}
