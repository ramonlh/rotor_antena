#include <WiFi.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "Arduino_JSON.h"
#include <Wire.h>
#include "JY901_Serial.h"
#include "HardwareSerial.h"
#include <UnixTime.h>

#include "config.h"
#include "app_state.h"
#include "imu_sensor.h"
#include "rotor_io.h"
#include "rotor_axes.h"
#include "rotor_positioning.h"
#include "sat_tracking.h"
#include "web_ui.h"
#include "network_services.h"
#include "storage_services.h"
#include "sat_favorites.h"

void setup() {
  Serial.begin(115200);
  initImu();
  initRotorPins();
  initWifi();
  initWebUI();
  server.begin();
  Serial.println("Servidor HTTP iniciado");
  initOta();
  getPositions();
  initStorage();
  initSatFavorites();
  initFtpServer();
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  handleFtpServer();

  updateElevationMove();
  updateAzimuthMove();
  updateOrientationMove();
  updateSimulationMotion();

  if ((millis() - timegetpositions) > timedelay120) {
    getPositions();
  }

  if ((millis() - time1) > kTickMs) {
    tickEachSecond();
  }
}
