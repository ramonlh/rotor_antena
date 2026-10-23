#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "network_services.h"
#include "app_state.h"
#include "config.h"


bool isWifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

const char* getWifiStatusText() {
  static char status[56];
  if (WiFi.status() == WL_CONNECTED) {
    const IPAddress ip = WiFi.localIP();
    snprintf(status, sizeof(status), "STA conectada - IP: %u.%u.%u.%u",
             ip[0], ip[1], ip[2], ip[3]);
    return status;
  }

  wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_AP || mode == WIFI_AP_STA) {
    const IPAddress ip = WiFi.softAPIP();
    snprintf(status, sizeof(status), "AP activo - IP: %u.%u.%u.%u",
             ip[0], ip[1], ip[2], ip[3]);
    return status;
  }

  return "WiFi no conectada";
}

void initWifi() {
  WiFi.mode(WIFI_STA);
  if (!WiFi.config(LOCAL_IP, GATEWAY_IP, SUBNET_MASK, PRIMARY_DNS, SECONDARY_DNS)) {
    Serial.println("STA Failed to configure");
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("");

  int attempts = 0;
  while ((WiFi.status() != WL_CONNECTED) && (attempts < 30)) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.print("Conectado a ");
    Serial.println(WIFI_SSID);
    Serial.print("Direccion IP: ");
    Serial.println(WiFi.localIP());
  } else {
    if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
      log_e("Soft AP creation failed.");
      while (true) {
        delay(1000);
      }
    }
    IPAddress myIP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(myIP);
  }
}

void initOta() {
  // Puerto OTA fijo: evita depender de una propiedad mDNS que algunas
  // versiones del IDE/core ESP32 no trasladan a la receta de subida.
  ArduinoOTA.setPort(3232);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA
    .onStart([]() {
      String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();
  Serial.println("OTA Ready");
}
