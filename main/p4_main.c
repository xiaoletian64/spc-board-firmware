/* p4_main.c — ESP32-P4-WIFI6 board app entry (phone-hand lineage).
 * M1: NVS + adb protocol self-check + OTA guard bring-up.
 * M2+ wires transport_usb (TinyUSB HS host), C6 WiFi (esp_wifi_remote),
 * engine bridge, DSI UI, BLE unlock. */
#include <stdio.h>
#include "board.h"
#include "ota_guard.h"
#include "adb_protocol.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "p4hub";

static int protocol_selftest(void)
{
    uint8_t mod[256], dexp[256], tx[1024];
    memset(mod, 0x11, sizeof mod); memset(dexp, 0x22, sizeof dexp);
    adb_auth_set_key(mod, sizeof mod, dexp, sizeof dexp);

    adb_session_t s; adb_session_reset(&s);
    uint8_t cnxn[24];
    uint32_t cmd = A_CNXN, magic = cmd ^ 0xffffffffu;
    for (int i = 0; i < 4; i++) {
        cnxn[i] = (cmd >> (8 * i)) & 0xff;
        cnxn[4 + i] = cnxn[20 + i] = (magic >> (8 * i)) & 0xff;
    }
    memset(cnxn + 8, 0, 12);
    adb_msg_t m;
    if (adb_msg_parse(cnxn, 24, &m) != 0) return -1;
    if (adb_session_on_msg(&s, &m, tx, sizeof tx) != 0) return -2;
    if (s.state != ADB_ST_AUTH || s.peer_maxdata != ADB_MAXDATA) return -3;
    return 0;
}

void app_main(void)
{
    ESP_LOGI(TAG, "phone-hand p4 hub boot (board=" BOARD_NAME ")");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    int rc = protocol_selftest();
    if (rc == 0) ESP_LOGI(TAG, "adb protocol selftest PASS");
    else ESP_LOGE(TAG, "adb protocol selftest FAIL rc=%d", rc);

    /* M2: tinyusb hs host -> adb transport (CNXN->AUTH->shell first light)
     * M3: esp_wifi_remote (C6) STA -> engine bridge -> HTTPS OTA (lineage)
     * M4: MIPI-DSI task card UI + BLE unlock via C6 */
    while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}
