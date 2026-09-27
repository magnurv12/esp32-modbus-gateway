#include "wifi_setup.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "config.h"

void connectWifiAndMdns() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Conectando ao WiFi '%s'", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print('.');
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }
  digitalWrite(LED_PIN, LOW);
  Serial.printf("\nWiFi conectado, IP: %s\n", WiFi.localIP().toString().c_str());

  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", HTTP_PORT);
    Serial.printf("mDNS ativo: http://%s.local/\n", MDNS_HOSTNAME);
  } else {
    Serial.println("Falha ao iniciar mDNS (acesso ainda funciona pelo IP acima)");
  }
}
