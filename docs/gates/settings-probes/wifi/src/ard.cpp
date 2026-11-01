#include <Arduino.h>
#include <WiFi.h>
void setup() {
  Serial.begin(115200);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks(false, false, true, 120);
  WiFi.begin("x", "y");
  configTime(0, 0, "pool.ntp.org");
  delay(5000);
  WiFi.mode(WIFI_OFF);
  Serial.println(n);
}
void loop() { delay(1000); }
