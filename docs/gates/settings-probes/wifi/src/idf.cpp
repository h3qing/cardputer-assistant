// Minimal IDF-level STA: passive scan, connect, SNTP, then full stop/deinit.
#include <Arduino.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_sntp.h>
static esp_netif_t* sta = nullptr;
static void wifi_up() {
  static bool once = false;
  if (!once) { esp_netif_init(); esp_event_loop_create_default(); sta = esp_netif_create_default_wifi_sta(); once = true; }
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  cfg.static_rx_buf_num = 4; cfg.dynamic_rx_buf_num = 8; cfg.tx_buf_type = 1; cfg.dynamic_tx_buf_num = 8;
  cfg.static_tx_buf_num = 0; cfg.cache_tx_buf_num = 4; cfg.ampdu_rx_enable = 0; cfg.ampdu_tx_enable = 0;
  cfg.csi_enable = 0; cfg.nvs_enable = 0; cfg.rx_ba_win = 0;
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();
}
static void wifi_down() { esp_wifi_disconnect(); esp_wifi_stop(); esp_wifi_deinit(); }
void setup() {
  Serial.begin(115200);
  Serial.printf("heap0 %u %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  wifi_up();
  wifi_scan_config_t sc = {}; sc.scan_type = WIFI_SCAN_TYPE_PASSIVE; sc.scan_time.passive = 120;
  esp_wifi_scan_start(&sc, true);
  uint16_t n = 20; wifi_ap_record_t recs[20]; esp_wifi_scan_get_ap_records(&n, recs);
  wifi_config_t wc = {}; strcpy((char*)wc.sta.ssid, "x"); strcpy((char*)wc.sta.password, "y");
  esp_wifi_set_config(WIFI_IF_STA, &wc); esp_wifi_connect();
  sntp_setoperatingmode(SNTP_OPMODE_POLL); sntp_setservername(0, "pool.ntp.org"); sntp_init();
  delay(5000);
  sntp_stop();
  wifi_down();
  Serial.printf("heap1 %u %u n=%u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), n);
}
void loop() { delay(1000); }
