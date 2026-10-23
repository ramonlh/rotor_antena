#include <WiFi.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <HTTPClient.h>
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
#include "sat_fetch.h"
#include "web_ui.h"
#include "network_services.h"
#include "storage_services.h"
#include "sat_favorites.h"
#include "sat_visible_now.h"
#include "ntfy_notifications.h"

void setup() {
  Serial.begin(115200);
  initImu();
  initRotorPins();
  initWifi();
  initPositionsService();

  // FFat conserva únicamente datos persistentes; la interfaz vive en flash.
  initStorage();
  restoreImuAzimuthReference();
  initSatFavorites();

  initWebUI();
  server.begin();
  initOta();
  getPositions();
  initFtpServer();
}

void loop() {
  // Publica resultados terminados por la tarea HTTPS sin bloquear.
  servicePositions();
  serviceVisibleNowAutoRefresh();
  serviceNtfyNotifications();

  // PRIORIDAD 1: sensor y control de motores.
  // Nunca comprobar el watchdog con una posicion anterior a una
  // posible espera/bloqueo de WebServer, OTA o FTP.
  static unsigned long lastImuMs = 0;
  const unsigned long nowMs = millis();

  if (!simulationMode && (nowMs - lastImuMs) >= 20UL) {
    lastImuMs = nowMs;
    updateAntennaPosition();
  }

  // El control del rotor va inmediatamente despues de actualizar
  // la posicion. Esto evita falsas paradas del watchdog si una
  // peticion web tarda varios segundos.
  if (isRotorReferenceBusy()) {
    updateRotorReference();
  } else {
    updateElevationMove();
    updateAzimuthMove();
    updateOrientationMove();
    updateSimulationMotion();
  }

  // PRIORIDAD 2: tareas de red.
  ArduinoOTA.handle();
  server.handleClient();
  handleFtpServer();

  // Sólo encola trabajo: HTTPS/JSON se ejecutan en el núcleo de red.
  if ((millis() - timegetpositions) > timedelay120) {
    getPositions();
  }

  if ((millis() - time1) > kTickMs) {
    tickEachSecond();
  }
}
