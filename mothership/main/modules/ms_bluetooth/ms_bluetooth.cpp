#include "esp_log.h"
/* BLE */
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

#include "ms_bluetooth.h"
#include "utils/ms_central_utils/esp_central.h"
#include "mothership_main.h"

extern "C" void ble_store_config_init(void);
static int ms_bluetooth_gap_event(struct ble_gap_event *event, void *arg);
static void ms_bluetooth_scan(void);

#define GATT_SVR_SVC_ENV_SENS_UUID 0x181A
#define GATT_SVR_SVC_ENV_SENS_CHR_HUMIDITY_UUID 0x2A6F

static BLEState *context = NULL;

static int blecent_should_connect(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    int rc;
    int i;

    /* The device has to be advertising connectability. */
    if (disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
        disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND)
    {

        return 0;
    }

    rc = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
    if (rc != 0)
    {
        return rc;
    }

    /* The device has to advertise support for the Alert Notification
     * service (0x1811).
     */
    for (i = 0; i < fields.num_uuids16; i++)
    {
        if (ble_uuid_u16(&fields.uuids16[i].u) == GATT_SVR_SVC_ENV_SENS_UUID)
        {
            return 1;
        }
    }

    return 0;
}

static void blecent_connect_if_interesting(const struct ble_gap_disc_desc *disc)
{
    uint8_t own_addr_type;
    int rc;

    /* Don't do anything if we don't care about this advertiser. */
    if (!blecent_should_connect(disc))
    {
        return;
    }

    /* Scanning must be stopped before a connection can be initiated. */
    rc = ble_gap_disc_cancel();
    if (rc != 0)
    {
        MODLOG_DFLT(DEBUG, "Failed to cancel scan; rc=%d\n", rc);
        return;
    }

    /* Figure out address to use for connect (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Try to connect the the advertiser.  Allow 30 seconds (30000 ms) for
     * timeout.
     */
    rc = ble_gap_connect(own_addr_type, &disc->addr, 30000, NULL,
                         ms_bluetooth_gap_event, NULL);
    if (rc != 0)
    {
        MODLOG_DFLT(ERROR, "Error: Failed to connect to device; addr_type=%d "
                           "addr=%s\n; rc=%d",
                    disc->addr.type, addr_str(disc->addr.val), rc);
        ms_bluetooth_scan();
        return;
    }
}

static void ms_bluetooth_on_reset(int reason)
{
    ESP_LOGE("mothership", "Resetting state; reason=%d\n", reason);
}

static void ms_bluetooth_on_sync(void)
{
    int rc;

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    ESP_LOGI("mothership", "Synced;");

    /* Begin scanning for a peripheral to connect to. */
    // blecent_scan();
    ms_bluetooth_scan();
}

static void ms_bluetooth_scan(void)
{
    uint8_t own_addr_type;
    struct ble_gap_disc_params disc_params;
    int rc;

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE("mothership", "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Tell the controller to filter duplicates; we don't want to process
     * repeated advertisements from the same device.
     */
    disc_params.filter_duplicates = 1;

    /**
     * Perform a passive scan.  I.e., don't send follow-up scan requests to
     * each advertiser.
     */
    disc_params.passive = 1;

    /* Use defaults for the rest of the parameters. */
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
                      ms_bluetooth_gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY)
    {
        MODLOG_DFLT(ERROR, "Error initiating GAP discovery procedure; rc=%d\n",
                    rc);
    }
}

static int ms_traverse_peer(const struct peer *peer, void *arg)
{
    if (context != NULL)
    {
        context->connectedPeers++;
    }
    return 0;
}

static void refresh_connected_peers_count()
{
    if (context != NULL)
    {
        context->connectedPeers = 0;
        peer_traverse_all(&ms_traverse_peer, NULL);
    }
}

static int ms_bluetooth_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    struct ble_hs_adv_fields fields;
    int rc;

    switch (event->type)
    {
    case BLE_GAP_EVENT_PASSKEY_ACTION:
    {
        MODLOG_DFLT(INFO, "passkey action event");
        struct ble_sm_io pkey = {0};
        if (event->passkey.params.action == BLE_SM_IOACT_INPUT)
        {
            MODLOG_DFLT(WARN, "Input not supported passing -> %d", context->peerPassword);
            pkey.action = event->passkey.params.action;
            pkey.passkey = context->peerPassword;
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        }
        return 0;
    }
    case BLE_GAP_EVENT_DISC:
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                     event->disc.length_data);
        if (rc != ESP_OK)
        {
            return 0;
        }

        /* An advertisment report was received during GAP discovery. */
        print_adv_fields(&fields);

        /* Try to connect to the advertiser if it looks interesting. */
        blecent_connect_if_interesting(&event->disc);
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        if (event->connect.status == 0)
        {
            /* Connection successfully established. */
            ESP_LOGI("mothership", "Connection established ");

            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            print_conn_desc(&desc);

            /* Remember peer. */
            rc = peer_add(event->connect.conn_handle);
            if (rc != 0 && rc != BLE_HS_EALREADY)
            {
                ESP_LOGE("mothership", "Failed to add peer; rc=%d\n", rc);
                return 0;
            }

            /** Initiate security - It will perform
             * Pairing (Exchange keys)
             * Bonding (Store keys)
             * Encryption (Enable encryption)
             * Will invoke event BLE_GAP_EVENT_ENC_CHANGE
             **/
            rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc != 0)
            {
                MODLOG_DFLT(INFO, "Security could not be initiated, rc = %d\n", rc);
                return ble_gap_terminate(event->connect.conn_handle,
                                         BLE_ERR_REM_USER_CONN_TERM);
            }
            else
            {
                MODLOG_DFLT(INFO, "Connection secured\n");
                ms_bluetooth_scan();
            }
            refresh_connected_peers_count();
        }
        else
        {
            /* Connection attempt failed; resume scanning. */
            MODLOG_DFLT(ERROR, "Error: Connection failed; status=%d\n",
                        event->connect.status);
            ms_bluetooth_scan();
        }

        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        /* Connection terminated. */
        MODLOG_DFLT(INFO, "disconnect; reason=%d ", event->disconnect.reason);
        print_conn_desc(&event->disconnect.conn);
        MODLOG_DFLT(INFO, "\n");

        /* Forget about peer. */
        peer_delete(event->disconnect.conn.conn_handle);

        /* Resume scanning. */
        ms_bluetooth_scan();
        refresh_connected_peers_count();
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        MODLOG_DFLT(INFO, "discovery complete; reason=%d\n",
                    event->disc_complete.reason);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Encryption has been enabled or disabled for this connection. */
        MODLOG_DFLT(INFO, "encryption change event; status=%d ",
                    event->enc_change.status);
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);
        print_conn_desc(&desc);
#if CONFIG_EXAMPLE_ENCRYPTION
        /*** Go for service discovery after encryption has been successfully enabled ***/
        rc = peer_disc_all(event->connect.conn_handle,
                           blecent_on_disc_complete, NULL);
        if (rc != 0)
        {
            MODLOG_DFLT(ERROR, "Failed to discover services; rc=%d\n", rc);
            return 0;
        }
#endif
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        /* Peer sent us a notification or indication. */
        MODLOG_DFLT(INFO, "received %s; conn_handle=%d attr_handle=%d "
                          "attr_len=%d\n",
                    event->notify_rx.indication ? "indication" : "notification",
                    event->notify_rx.conn_handle,
                    event->notify_rx.attr_handle,
                    OS_MBUF_PKTLEN(event->notify_rx.om));

        /* Attribute data is contained in event->notify_rx.om. Use
         * `os_mbuf_copydata` to copy the data received in notification mbuf */
        return 0;

    case BLE_GAP_EVENT_MTU:
        MODLOG_DFLT(INFO, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* We already have a bond with the peer, but it is attempting to
         * establish a new secure link.  This app sacrifices security for
         * convenience: just throw away the old bond and accept the new link.
         */

        /* Delete the old bond. */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        ble_store_util_delete_peer(&desc.peer_id_addr);

        /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
         * continue with the pairing operation.
         */
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    default:
        return 0;
    }
}

void ms_ble_start(Action *a)
{
    // !!! nvs_flash_init must be called before starting !!!
    esp_err_t res = nimble_port_init();

    if (res != ESP_OK)
    {
        ESP_LOGE("mothership", "Failed to init Nimble: %d", res);
        return;
    }

    ble_hs_cfg.reset_cb = ms_bluetooth_on_reset;
    ble_hs_cfg.sync_cb = ms_bluetooth_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_KEYBOARD_ONLY;

    /* Initialize data structures to track connected peers. */
    int rc = peer_init(3, 3, 3, 3);
    assert(rc == 0);

    /* Set the default device name. */
    rc = ble_svc_gap_device_name_set("mothership");
    assert(rc == 0);

    /* XXX Need to have template for store */
    ble_store_config_init();

    ESP_LOGI("mothership", "Nimble initialized");

    context = (BLEState *)a->context;
}

void ms_ble_tick(Action *a)
{
    ble_npl_eventq *eventQueue = nimble_port_get_dflt_eventq();

    struct ble_npl_event *ev;
    npl_funcs_t *nplFuncs = npl_freertos_funcs_get();

    ev = nplFuncs->p_ble_npl_eventq_get(eventQueue, portTICK_PERIOD_MS);
    if (ev)
    {
        nplFuncs->p_ble_npl_event_run(ev);
        // ESP_LOGI("mothership", "ms_ble: executed event");
    }
}

void ms_ble_stop(Action *a)
{
    // this is blocking because the event processing tick is no longer happening
    context->connectedPeers = 0;
    context = NULL;
    int rc = nimble_port_deinit();
    if (rc != ESP_OK)
    {
        ESP_LOGE("mothership", "Nimble stop failed; reason=%d", rc);
    }
    else
    {
        ESP_LOGI("mothership", "Nimble stopped");
    }
}