#include "wifiCreds.h"
#include <WiFi.h>
#include <ESPmDNS.h>

void startWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.println(WiFi.localIP());

  MDNS.begin(mDNSName);
}