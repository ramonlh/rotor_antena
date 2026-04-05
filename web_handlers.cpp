

#include <Arduino.h>
#include <Esp.h>
#include "web_handlers.h"
#include "web_pages.h"
#include "app_state.h"
#include "imu_sensor.h"
#include "satellite_tracker.h"
#include "rotor_io.h"
#include "rotor_positioning.h"

void handleIndex() {
  updateAntennaPosition();
  server.send(200, "text/html", buildHtml(true));
}

void handleSeguirSat() {
  enableseguirsat = !enableseguirsat;
  server.send(200, "text/html", buildHtml(false));
}

void handleIrAOrigen() {
  orientAntenna(0.0, 0.0);
  server.send(200, "text/html", buildHtml(false));
}

void handleOrientarManual() {
  orientAntenna(satazimut, satelevat);
  server.send(200, "text/html", buildHtml(false));
}

void handleArriba() {
  disableElevation();
  digitalWrite(kElev1pin, ON);
  digitalWrite(kElev2pin, ON);
  enableElevation();
  delay(500);
  disableElevation();
  delay(500);
  updateAntennaPosition();
  server.send(200, "text/html", buildHtml(false));
}

void handleAbajo() {
  disableElevation();
  digitalWrite(kElev1pin, OFF);
  digitalWrite(kElev2pin, OFF);
  enableElevation();
  delay(500);
  disableElevation();
  delay(500);
  updateAntennaPosition();
  server.send(200, "text/html", buildHtml(false));
}

void handleGiroIzda() {
  disableRotation();
  digitalWrite(kGiro1pin, OFF);
  digitalWrite(kGiro2pin, OFF);
  digitalWrite(kGiropin, ON);
  delay(2000);
  disableRotation();
  delay(500);
  updateAntennaPosition();
  server.send(200, "text/html", buildHtml(false));
}

void handleGiroDcha() {
  disableRotation();
  digitalWrite(kGiro1pin, ON);
  digitalWrite(kGiro2pin, ON);
  digitalWrite(kGiropin, ON);
  delay(2000);
  digitalWrite(kGiropin, OFF);
  delay(500);
  updateAntennaPosition();
  server.send(200, "text/html", buildHtml(false));
}

void handleReset() {
  ESP.restart();
}

void handleGetData() {
  getPositions();
  server.send(200, "text/html", buildHtml(false));
}

void handleNotFound() {
  server.send(404, "text/plain", "La pagina no existe");
}