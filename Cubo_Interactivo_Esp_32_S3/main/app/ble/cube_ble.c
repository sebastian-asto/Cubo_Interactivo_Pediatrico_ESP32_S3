#include "cube_ble.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "emotions.h"
#include "tft_st7789.h"

void ble_store_config_init(void);

/* UUID textual: 72a10000-6c75-426f-9000-6375626f0001; chars 0001/0002/0003. */
#define UUID(n)                                                                                    \
    BLE_UUID128_INIT(1, 0, 0x6f, 0x62, 0x75, 0x63, 0, 0x90, 0x6f, 0x42, 0x75, 0x6c, n, 0, 0xa1,    \
                     0x72)
static const ble_uuid128_t service_uuid = UUID(0), command_uuid = UUID(1), data_uuid = UUID(2),
                           status_uuid = UUID(3);
static SemaphoreHandle_t lock;
static QueueHandle_t queue;
static uint8_t own_type;
static uint16_t connection = BLE_HS_CONN_HANDLE_NONE;
static uint32_t connection_generation;
static bool ready;
/* status: version, result(0 OK/1 BUSY/2 ERROR), seqLE16, op, selected,
 * errLE32, receivedLE32, sd_ready, emotion_count. */
static uint8_t status[16] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16};
static uint8_t *upload;
static uint32_t received, expected_crc;
static uint8_t upload_index;
static TickType_t last_chunk;
static const size_t image_size = 115208;
typedef struct
{
    uint8_t bytes[12];
    uint16_t owner;
    uint32_t generation;
} command_t;
static uint32_t le32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32(uint8_t *p, uint32_t x)
{
    for (int i = 0; i < 4; i++)
        p[i] = x >> (i * 8);
}
static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffff;
    while (n--)
    {
        c ^= *p++;
        for (int i = 0; i < 8; i++)
            c = (c >> 1) ^ ((c & 1) ? 0xedb88320 : 0);
    }
    return ~c;
}
static void discard(void)
{
    free(upload);
    upload = NULL;
    received = 0;
}
static void worker(void *arg)
{
    command_t cmd;
    while (true)
    {
        if (xQueueReceive(queue, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE)
        {
            xSemaphoreTake(lock, portMAX_DELAY);
            if (upload && xTaskGetTickCount() - last_chunk > pdMS_TO_TICKS(60000))
                discard();
            xSemaphoreGive(lock);
            continue;
        }
        uint8_t op = cmd.bytes[0], idx = cmd.bytes[3];
        esp_err_t err = ESP_OK;
        uint8_t *image = NULL;
        xSemaphoreTake(lock, portMAX_DELAY);
        if (cmd.owner != connection || cmd.generation != connection_generation)
            err = ESP_ERR_INVALID_STATE;
        else if (!ready && op != 5)
            err = ESP_ERR_INVALID_STATE;
        else if (op == 2)
        {
            discard();
            if (idx >= 16 || le32(cmd.bytes + 4) != image_size)
                err = ESP_ERR_INVALID_SIZE;
            else
            {
                upload = heap_caps_malloc(image_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!upload)
                    err = ESP_ERR_NO_MEM;
                else
                {
                    upload_index = idx;
                    expected_crc = le32(cmd.bytes + 8);
                    last_chunk = xTaskGetTickCount();
                }
            }
        }
        else if (op == 3)
        {
            if (!upload || received != image_size || crc32(upload, received) != expected_crc)
                err = ESP_ERR_INVALID_CRC;
            else
            {
                image = upload;
                upload = NULL;
                idx = upload_index;
            }
            discard();
        }
        else if (op == 5)
            discard();
        else if (upload)
            err = ESP_ERR_INVALID_STATE;
        xSemaphoreGive(lock);
        /* Only this task uses SD and LCD after startup. Never block the BLE host on IO. */
        if (err == ESP_OK)
        {
            if (op == 1)
                err = emotions_show(idx);
            else if (op == 3)
            {
                err = emotions_replace(idx, image, image_size);
                if (err == ESP_OK)
                    err = emotions_show(idx);
            }
            else if (op == 4)
            {
                for (size_t i = 0; i < emotions_count() && err == ESP_OK; i++)
                    err = emotions_restore(i);
                idx = 0;
                if (err == ESP_OK)
                    err = emotions_show(idx);
            }
            else if (op == 6)
            {
                err = emotions_restore(idx);
                if (err == ESP_OK)
                    err = emotions_show(idx);
            }
        }
        free(image);
        xSemaphoreTake(lock, portMAX_DELAY);
        if (err == ESP_OK && (op == 1 || op == 3 || op == 4 || op == 6))
            status[5] = idx;
        status[1] = err == ESP_OK ? 0 : 2;
        put32(status + 6, (uint32_t)err);
        xSemaphoreGive(lock);
        ESP_LOGI("CUBE_BLE", "op=%u emotion=%u result=%s", op, idx, esp_err_to_name(err));
    }
}
static int access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg)
{
    uintptr_t kind = (uintptr_t)arg;
    int rc = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (kind == 3)
    {
        put32(status + 10, received);
        rc = os_mbuf_append(ctx->om, status, sizeof status) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    else
    {
        uint16_t len = OS_MBUF_PKTLEN(ctx->om);
        uint8_t packet[512];
        if (len > sizeof packet || ble_hs_mbuf_to_flat(ctx->om, packet, sizeof packet, NULL) != 0)
            rc = BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        else if (kind == 2)
        {
            if (status[1] == 1 || !upload || len < 5 || le32(packet) != received ||
                len - 4 > image_size - received)
                rc = BLE_ATT_ERR_UNLIKELY;
            else
            {
                memcpy(upload + received, packet + 4, len - 4);
                received += len - 4;
                last_chunk = xTaskGetTickCount();
            }
        }
        else if (len != 12 || packet[0] < 1 || packet[0] > 6 || packet[3] >= 16)
            rc = BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        else if (status[1] == 1)
            rc = BLE_ATT_ERR_INSUFFICIENT_RES;
        else
        {
            command_t cmd = {.owner = conn, .generation = connection_generation};
            memcpy(cmd.bytes, packet, 12);
            if (xQueueSend(queue, &cmd, 0) != pdTRUE)
                rc = BLE_ATT_ERR_INSUFFICIENT_RES;
            else
            {
                status[1] = 1;
                status[2] = packet[1];
                status[3] = packet[2];
                status[4] = packet[0];
                put32(status + 6, 0);
            }
        }
    }
    xSemaphoreGive(lock);
    return rc;
}
static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &service_uuid.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){{.uuid = &command_uuid.u,
                                      .access_cb = access_cb,
                                      .arg = (void *)1,
                                      .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC},
                                     {.uuid = &data_uuid.u,
                                      .access_cb = access_cb,
                                      .arg = (void *)2,
                                      .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC},
                                     {.uuid = &status_uuid.u,
                                      .access_cb = access_cb,
                                      .arg = (void *)3,
                                      .flags = BLE_GATT_CHR_F_READ},
                                     {0}}},
    {0}};
static void advertise(void);
static int gap_event(struct ble_gap_event *ev, void *arg)
{
    if (ev->type == BLE_GAP_EVENT_CONNECT)
    {
        if (ev->connect.status == 0)
        {
            xSemaphoreTake(lock, portMAX_DELAY);
            connection = ev->connect.conn_handle;
            status[14] = ready ? 1 : 0;
            xSemaphoreGive(lock);
            int rc = ble_gap_security_initiate(ev->connect.conn_handle);
            ESP_LOGI("CUBE_BLE", "Conectado, iniciar cifrado: %d", rc);
        }
        else
            advertise();
    }
    else if (ev->type == BLE_GAP_EVENT_DISCONNECT)
    {
        xSemaphoreTake(lock, portMAX_DELAY);
        connection = BLE_HS_CONN_HANDLE_NONE;
        status[14] = ready ? 1 : 0;
        ++connection_generation;
        discard();
        xSemaphoreGive(lock);
        advertise();
    }
    else if (ev->type == BLE_GAP_EVENT_ENC_CHANGE)
    {
        struct ble_gap_conn_desc desc;
        bool encrypted = ev->enc_change.status == 0 &&
                         ble_gap_conn_find(ev->enc_change.conn_handle, &desc) == 0 &&
                         desc.sec_state.encrypted;
        xSemaphoreTake(lock, portMAX_DELAY);
        status[14] = (ready ? 1 : 0) | (encrypted ? 2 : 0);
        xSemaphoreGive(lock);
        ESP_LOGI("CUBE_BLE", "Cifrado=%d status=%d", encrypted, ev->enc_change.status);
    }
    else if (ev->type == BLE_GAP_EVENT_REPEAT_PAIRING)
    {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &desc) == 0)
            ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    else if (ev->type == BLE_GAP_EVENT_ADV_COMPLETE)
        advertise();
    return 0;
}
static void advertise(void)
{
    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128 = (ble_uuid128_t *)&service_uuid;
    f.num_uuids128 = 1;
    f.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);
    struct ble_hs_adv_fields scan = {0};
    scan.name = (uint8_t *)"Cubo-ESP";
    scan.name_len = 8;
    scan.name_is_complete = 1;
    if (!rc)
        rc = ble_gap_adv_rsp_set_fields(&scan);
    struct ble_gap_adv_params p = {.conn_mode = BLE_GAP_CONN_MODE_UND,
                                   .disc_mode = BLE_GAP_DISC_MODE_GEN};
    if (!rc)
        rc = ble_gap_adv_start(own_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    ESP_LOGI("CUBE_BLE", "Advertising Cubo-ESP: %d", rc);
}
static void sync_cb(void)
{
    if (ble_hs_id_infer_auto(0, &own_type) == 0)
        advertise();
}
static void host_task(void *p)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}
esp_err_t cube_ble_start(bool sd_ready)
{
    ready = sd_ready;
    status[14] = sd_ready;
    lock = xSemaphoreCreateMutex();
    queue = xQueueCreate(1, sizeof(command_t));
    if (!lock || !queue)
        return ESP_ERR_NO_MEM;
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK)
        return err; /* Never erase user NVS implicitly. */
    err = nimble_port_init();
    if (err != ESP_OK)
        return err;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("Cubo-ESP");
    ble_hs_cfg.sync_cb = sync_cb;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();
    /* Just Works with persistent keys; this is not exclusive device ownership. */
    if (ble_gatts_count_cfg(services) || ble_gatts_add_svcs(services))
        return ESP_FAIL;
    if (xTaskCreate(worker, "cube_commands", 6144, NULL, 5, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
