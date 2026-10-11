#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "os/os_mbuf.h"
#include "ble_status.h"
#include "protocol.h"

static const char *TAG = "dashboard_ble";
static QueueHandle_t ui_queue;
static dashboard_message_t message;
static uint8_t own_addr_type;
typedef struct { uint32_t id; char name[8]; } dashboard_command_t;
static QueueHandle_t command_queue;
static uint32_t command_id;
static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(
    0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,
    0x93,0xf3,0xa3,0xb5,0x01,0x00,0x40,0x6e);
static const ble_uuid128_t write_uuid = BLE_UUID128_INIT(
    0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,
    0x93,0xf3,0xa3,0xb5,0x02,0x00,0x40,0x6e);
static const ble_uuid128_t control_uuid = BLE_UUID128_INIT(
    0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,
    0x93,0xf3,0xa3,0xb5,0x03,0x00,0x40,0x6e);

static void publish(void) { xQueueOverwrite(ui_queue, &message); }
static void advertise(void);

static int access_status(uint16_t connection, uint16_t attribute,
                         struct ble_gatt_access_ctxt *context, void *arg)
{
    (void)connection; (void)attribute; (void)arg;
    if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    unsigned int len = OS_MBUF_PKTLEN(context->om);
    if (!len || len >= 24) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    char text[24] = {0};
    if (os_mbuf_copydata(context->om, 0, len, text))
        return BLE_ATT_ERR_UNLIKELY;
    if (strlen(text) != len) return BLE_ATT_ERR_UNLIKELY;
    if (len > 4 && !memcmp(text, "ACK:", 4)) {
        const char *p = text + 4;
        uint32_t id;
        if (!dashboard_number(&p, UINT32_MAX, &id) || *p) return BLE_ATT_ERR_UNLIKELY;
        dashboard_command_t command;
        if (xQueuePeek(command_queue, &command, 0) == pdTRUE && command.id == id)
            xQueueReceive(command_queue, &command, 0);
        return 0;
    }
    if (len > 4 && !memcmp(text, "M:", 2)) {
        if (len > 20 || (!dashboard_metadata(text, message.project, &message.running,
                                           &message.waiting, &message.seen, &message.elapsed) &&
                         !dashboard_extra(text, &message.extra)))
            return BLE_ATT_ERR_UNLIKELY;
        message.app_source = true;
        publish();
        return 0;
    }
    bool app_source = len > 4 && memcmp(text, "APP:", 4) == 0;
    const char *value = text + (app_source ? 4 : 0);
    unsigned int value_len = len - (app_source ? 4 : 0);
    static const char *names[] = {"IDLE", "WORKING", "WAITING", "DONE", "ERROR", "UNKNOWN"};
    for (unsigned int i = 0; i < 6; i++) {
        if (value_len == strlen(names[i]) && memcmp(value, names[i], value_len) == 0) {
            message.state = i;
            message.app_source = app_source;
            message.has_status = true;
            publish();
            ESP_LOGI(TAG, "Received %s status: %s", app_source ? "APP" : "TEST", names[i]);
            return 0;
        }
    }
    return BLE_ATT_ERR_UNLIKELY;
}

bool dashboard_send_command(const char *name)
{
    if (!command_queue || strlen(name) > 7) return false;
    dashboard_command_t command = { .id = ++command_id };
    if (!command.id) command.id = ++command_id;
    strcpy(command.name, name);
    return xQueueSend(command_queue, &command, 0) == pdTRUE;
}

bool dashboard_command_pending(void)
{
    return command_queue && uxQueueMessagesWaiting(command_queue) > 0;
}

static int access_control(uint16_t connection, uint16_t attribute,
                          struct ble_gatt_access_ctxt *context, void *arg)
{
    (void)connection; (void)attribute; (void)arg;
    if (context->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_READ_NOT_PERMITTED;
    dashboard_command_t command;
    if (xQueuePeek(command_queue, &command, 0) != pdTRUE) return 0;
    char text[24];
    int len = snprintf(text, sizeof(text), "C:%lu:%s", (unsigned long)command.id, command.name);
    return os_mbuf_append(context->om, text, len) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}

static const struct ble_gatt_svc_def services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = &write_uuid.u, .access_cb = access_status,
              .flags = BLE_GATT_CHR_F_WRITE },
            { .uuid = &control_uuid.u, .access_cb = access_control,
              .flags = BLE_GATT_CHR_F_READ },
            {0}
        }
    },
    {0}
};

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        memset(&message, 0, sizeof(message));
        message.connected = event->connect.status == 0;
        message.has_status = false;
        publish();
        if (!message.connected) advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        message.connected = false;
        message.has_status = false;
        message.extra.running = false;
        xQueueReset(command_queue);
        publish();
        advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    default: break;
    }
    return 0;
}

static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc) { ESP_LOGE(TAG, "Advertising fields: %d", rc); return; }
    struct ble_hs_adv_fields response = {0};
    response.uuids128 = (ble_uuid128_t *)&service_uuid;
    response.num_uuids128 = 1;
    response.uuids128_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc) { ESP_LOGE(TAG, "Scan response: %d", rc); return; }
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &params, gap_event, NULL);
    if (rc) ESP_LOGE(TAG, "Advertising start: %d", rc);
    else {
        publish();
        ESP_LOGI(TAG, "Advertising as Codex-Dashboard");
    }
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    assert(rc == 0);
    advertise();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "Host reset: %d", reason);
    message.connected = false;
    message.has_status = false;
    publish();
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_dashboard_start(QueueHandle_t queue)
{
    ui_queue = queue;
    command_queue = xQueueCreate(1, sizeof(dashboard_command_t));
    assert(command_queue != NULL);
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set("Codex-Dashboard");
    assert(rc == 0);
    rc = ble_gatts_count_cfg(services);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(services);
    assert(rc == 0);
    nimble_port_freertos_init(host_task);
}
