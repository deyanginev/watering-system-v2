#include "nvs_flash.h"

/* own services */
#include "voyager_main.h"

/* BLE */
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/ans/ble_svc_ans.h"
#include "nimble/nimble_port_freertos.h"
#include "characteristics/voyager_primary.h"
#include "esp_random.h"
#include "modules/actions/actions.h"

extern "C" void ble_store_config_init(void);

extern char *voyager_tag;

static void voyager_advertise(void);

static uint8_t own_addr_type;

static ble_uuid16_t *advertisedService = (ble_uuid16_t *)calloc(1, sizeof(ble_uuid16_t));
static voyager_app_context *context;

static void
voyager_print_addr(const void *addr)
{
    const uint8_t *u8p;

    u8p = (uint8_t *)addr;
    MODLOG_DFLT(INFO, "%02x:%02x:%02x:%02x:%02x:%02x",
                u8p[5], u8p[4], u8p[3], u8p[2], u8p[1], u8p[0]);
}

static void voyager_print_conn_desc(struct ble_gap_conn_desc *desc)
{
    MODLOG_DFLT(INFO, "handle=%d our_ota_addr_type=%d our_ota_addr=",
                desc->conn_handle, desc->our_ota_addr.type);
    voyager_print_addr(desc->our_ota_addr.val);
    MODLOG_DFLT(INFO, " our_id_addr_type=%d our_id_addr=",
                desc->our_id_addr.type);
    voyager_print_addr(desc->our_id_addr.val);
    MODLOG_DFLT(INFO, " peer_ota_addr_type=%d peer_ota_addr=",
                desc->peer_ota_addr.type);
    voyager_print_addr(desc->peer_ota_addr.val);
    MODLOG_DFLT(INFO, " peer_id_addr_type=%d peer_id_addr=",
                desc->peer_id_addr.type);
    voyager_print_addr(desc->peer_id_addr.val);
    MODLOG_DFLT(INFO, " conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                      "encrypted=%d authenticated=%d bonded=%d\n",
                desc->conn_itvl, desc->conn_latency,
                desc->supervision_timeout,
                desc->sec_state.encrypted,
                desc->sec_state.authenticated,
                desc->sec_state.bonded);
}

static int voyager_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type)
    {
    // case BLE_GAP_EVENT_LINK_ESTAB:
    // {
    //     MODLOG_DFLT(INFO, "link established %s; status=%d ",
    //                 event->connect.status == 0 ? "established" : "failed",
    //                 event->connect.status);
    //     if (event->connect.status == 0)
    //     {
    //         rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
    //         assert(rc == 0);
    //         voyager_print_conn_desc(&desc);
    //     }

    //     if (event->connect.status != 0)
    //     {
    //         voyager_advertise();
    //     }
    //     return 0;
    // }
    // break;
    case BLE_GAP_EVENT_CONNECT:
    {
        MODLOG_DFLT(INFO, "connection %s; status=%d ",
                    event->connect.status == 0 ? "established" : "failed",
                    event->connect.status);
        if (event->connect.status == 0)
        {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            voyager_print_conn_desc(&desc);
        }

        if (!desc.sec_state.encrypted)
        {
            context->ble.state = VY_BLE_STATE_CONNECTING;
            rc = ble_gap_security_initiate(desc.conn_handle);

            if (rc != ESP_OK)
            {
                MODLOG_ERROR(ERROR, "could not initiate security procedure: %d", rc);
                return rc;
            }
            return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        }
        else
        {
            context->ble.state = VY_BLE_STATE_CONNECTED;
        }
        return 0;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING:
    {
        MODLOG_INFO(INFO, "repeat pairing requested");
        int rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);

        if (rc != ESP_OK)
        {
            MODLOG_ERROR(ERROR, "could not find connection: %d with code: %d", event->repeat_pairing.conn_handle, rc);
            return rc;
        }

        ble_store_util_delete_peer(&desc.peer_id_addr);

        MODLOG_INFO(INFO, "repeat pairing...");

        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    break;
    case BLE_GAP_EVENT_DISCONNECT:
    {
        MODLOG_DFLT(INFO, "disconnect; reason=%d ", event->disconnect.reason);
        voyager_print_conn_desc(&event->disconnect.conn);
        voyager_advertise();
        context->ble.state = VY_BLE_STATE_DISCONNECTED;
        return 0;
    }
    break;
    case BLE_GAP_EVENT_CONN_UPDATE:
    {
        MODLOG_DFLT(INFO, "connection updated; status=%d ", event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        if (rc != ESP_OK)
        {
            MODLOG_ERROR(ERROR, "could not find a connection");
        }
        else
        {
            voyager_print_conn_desc(&desc);
        }
        return 0;
    }
    break;
    case BLE_GAP_EVENT_ENC_CHANGE:
    {
        /* Encryption has been enabled or disabled for this connection. */
        MODLOG_DFLT(INFO, "encryption change event; status=%d ",
                    event->enc_change.status);
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);
        voyager_print_conn_desc(&desc);

        if (!desc.sec_state.encrypted)
        {
            MODLOG_DFLT(INFO, "not encrypted, retrying; status=%d ",
                        event->enc_change.status);
            ble_store_util_delete_peer(&desc.peer_id_addr);
            return ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        else
        {
            context->ble.state = VY_BLE_STATE_CONNECTED;
        }
        return 0;
    }
    break;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
    {
        ESP_LOGI(voyager_tag, "PASSKEY_ACTION_EVENT started");

        if (event->passkey.params.action == BLE_SM_IOACT_DISP)
        {
            struct ble_sm_io pkey = {0};
            pkey.action = event->passkey.params.action;
            pkey.passkey = 123456; // 100000 + esp_random() % 900000;
            MODLOG_WARN(WARN, "enter passkey: %" PRIu32 " on the peer side", pkey.passkey);

            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            if (rc != ESP_OK)
            {
                MODLOG_ERROR(ERROR, "error enforcing passkey: %d", rc);
                return rc;
            }
        }

        return 0;
    }
    break;
    }

    return 1;
}

static void voyager_advertise()
{
    ESP_LOGI(voyager_tag, "Starting advertising...");
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;
    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    name = ble_svc_gap_device_name();

    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    advertisedService[0] = BLE_UUID16_INIT(GATT_SVR_SVC_ENV_SENS_UUID);

    fields.uuids16 = advertisedService;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != ESP_OK)
    {
        ESP_LOGE(voyager_tag, "Error occurred while setting up adv: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, voyager_gap_event, NULL);
    if (rc != 0)
    {
        MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
        return;
    }
    ESP_LOGI(voyager_tag, "Started advertising...");
}

static void on_ble_reset(int reason)
{
    ESP_LOGW(voyager_tag, "Host reset: %d", reason);
}

// static void set_random_addr(void)
// {
//     /* Local variables */
//     int rc = 0;
//     ble_addr_t addr;

//     /* Generate new non-resolvable private address */
//     rc = ble_hs_id_gen_rnd(0, &addr);
//     assert(rc == 0);

//     /* Set address */
//     rc = ble_hs_id_set_rnd(addr.val);
//     assert(rc == 0);
// }

static void on_ble_sync()
{
    ESP_LOGI(voyager_tag, "Sync callback");

    int rc;
    own_addr_type = BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT;

    rc = ble_hs_util_ensure_addr(1);

    if (rc != ESP_OK)
    {
        ESP_LOGE(voyager_tag, "Failed to ensure address: %d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(1, &own_addr_type);
    if (rc != ESP_OK)
    {
        ESP_LOGE(voyager_tag, "Failed to ensure address: %d", rc);
        return;
    }

    // uint8_t addr_val[6] = {0};
    // rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);

    // if (rc != ESP_OK)
    // {
    //     ESP_LOGE(voyager_tag, "Failed to copy address to local: %d", rc);
    // }
    // voyager_print_addr(addr_val);
    ESP_LOGW(voyager_tag, "Own address type: %d", own_addr_type);
    voyager_advertise();
}

static void on_ble_gatt_server_register(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_SVC:
        MODLOG_DFLT(DEBUG, "registered service %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                    ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        MODLOG_DFLT(DEBUG, "registering characteristic %s with "
                           "def_handle=%d val_handle=%d\n",
                    ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                    ctxt->chr.def_handle,
                    ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        MODLOG_DFLT(DEBUG, "registering descriptor %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                    ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}

static int on_ble_store_status(struct ble_store_status_event *event, void *arg)
{
    ESP_LOGI(voyager_tag, "Store reported: %d", event->event_code);
    return 0;
}

int initialize(struct voyager_app_context *ctx)
{
    int rc = nimble_port_init();
    ESP_ERROR_CHECK(rc);

    if (rc != ESP_OK)
    {
        ESP_LOGE(voyager_tag, "Failed to init nimble: %d", rc);
        return rc;
    }

    // configure ble host
    ble_hs_cfg.reset_cb = on_ble_reset;
    ble_hs_cfg.sync_cb = on_ble_sync;
    ble_hs_cfg.gatts_register_cb = on_ble_gatt_server_register;
    ble_hs_cfg.store_status_cb = on_ble_store_status;
    ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;

    // init server table
    ESP_LOGI(voyager_tag, "Initializing server table...");

    // base services
    // ble_svc_gap_init();
    // ble_svc_gatt_init();
    // ble_svc_ans_init();
    rc = initialize_voyager_primary(ctx);

    if (rc != ESP_OK)
    {
        ESP_LOGE(voyager_tag, "Failed to initialize primary service: %d", rc);
        return rc;
    }

    // device name
    rc = ble_svc_gap_device_name_set("voyager");
    if (rc != ESP_OK)
    {
        ESP_LOGE(voyager_tag, "Failed to set device name: %d", rc);
        return rc;
    }

    /* XXX Need to have template for store */
    ble_store_config_init();

    return ESP_OK;
}

void vy_ble_start(Action *a)
{
    context = (voyager_app_context *)a->context;
    int rc = initialize(context);
    if (rc != ESP_OK)
    {
        ESP_LOGE(voyager_tag, "Could not initialize BLE: %d", rc);
    }
}
void vy_ble_tick(Action *a)
{
    ble_npl_eventq *eventQueue = nimble_port_get_dflt_eventq();

    struct ble_npl_event *ev;
    npl_funcs_t *nplFuncs = npl_freertos_funcs_get();

    ev = nplFuncs->p_ble_npl_eventq_get(eventQueue, portTICK_PERIOD_MS);
    if (ev)
    {
        nplFuncs->p_ble_npl_event_run(ev);
    }
}
void vy_ble_stop(Action *a)
{
    int rc = nimble_port_deinit();
    if (rc != ESP_OK)
    {
        ESP_LOGE(voyager_tag, "Nimble stop failed; reason=%d", rc);
    }
    else
    {
        ESP_LOGI(voyager_tag, "Nimble stopped");
    }
    context = nullptr;
}