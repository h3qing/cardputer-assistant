// Scan-only: no esp_netif, no lwIP use, no connect, no SNTP.
#include <Arduino.h>
#include <esp_wifi.h>
#include <esp_event.h>
void setup() {
  Serial.begin(115200);
  esp_event_loop_create_default();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  cfg.static_rx_buf_num = 4; cfg.dynamic_rx_buf_num = 8; cfg.ampdu_rx_enable = 0; cfg.ampdu_tx_enable = 0;
  cfg.csi_enable = 0; cfg.nvs_enable = 0;
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();
  wifi_scan_config_t sc = {}; sc.scan_type = WIFI_SCAN_TYPE_PASSIVE; sc.scan_time.passive = 120;
  esp_wifi_scan_start(&sc, true);
  uint16_t n = 20; wifi_ap_record_t recs[20]; esp_wifi_scan_get_ap_records(&n, recs);
  esp_wifi_stop(); esp_wifi_deinit();
  Serial.println(n);
}
void loop() { delay(1000); }
