

#include <Arduino.h>
#include <Esp.h>
#include "web_handlers.h"
#include "web_pages.h"
#include "app_state.h"
#include "imu_sensor.h"
#include "sat_tracking.h"
#include "rotor_io.h"
#include "rotor_positioning.h"
#include "sat_catalog.h"
#include "sat_favorites.h"
#include "sat_search.h"
#include "sat_visibility.h"
#include "sat_visible_now.h"
#include "sat_radio_info.h"

void handleIndex() {
  bool satChanged = false;

  if (server.hasArg("SATS")) {
    long newSat = server.arg("SATS").toInt();
    if (newSat > 0 && newSat != satid) {
      satid = newSat;
      satChanged = true;
    }
  }

  updateAntennaPosition();

  // Cargar posiciones al entrar, al cambiar satélite,
  // o si no hay datos / están viejos
  if (satChanged || tabunixtime[0] == 0 || (millis() - timegetpositions > 10000)) {
    getPositions();
    refreshRadioInfoForSat(satid);
  }

  server.send(200, "text/html", buildHtml(false));
}

void handleSeguirSat() {
  enableseguirsat = !enableseguirsat;

  if (enableseguirsat) {
    getPositions();

    // si no hay datos de prediccion, desactivar seguir
    if (tabunixtime[0] == 0) {
      enableseguirsat = false;
    }
  } else {
    stopAllMotion();
  }

  server.send(200, "text/html", buildHtml(false));
}

void handleIrAOrigen() {
  orientAntenna(0.0, 0.0);
  server.send(200, "text/html", buildHtml(false));
}

void handleOrientarManual() {
  if (server.hasArg("az") && server.hasArg("el")) {
    float az = server.arg("az").toFloat();
    float el = server.arg("el").toFloat();

    // normalización simple
    if (az < 0.0f) az = 0.0f;
    if (az > 360.0f) az = 360.0f;

    if (el < 0.0f) el = 0.0f;
    if (el > 90.0f) el = 90.0f;

    orientAntenna(az, el);
  }

  server.send(200, "text/html", buildHtml(false));
}

void handleArriba() {
  jogElevation(+5.0f);
  server.send(204, "text/plain", "");
}

void handleAbajo() {
  jogElevation(-5.0f);
  server.send(204, "text/plain", "");
}

void handleGiroIzda() {
  jogAzimuth(-5.0f);
  server.send(204, "text/plain", "");
}

void handleGiroDcha() {
  jogAzimuth(+5.0f);
  server.send(204, "text/plain", "");
}

void handleStop() {
  stopAllMotion();
  server.send(204, "text/plain", "");
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

void handleToggleSim() {
  simulationMode = !simulationMode;
  server.send(200, "text/html", buildHtml(false));
}

void handleAddFavorite() {
  loadSatFavorites();

  String name;

  if (satname.length() > 0) name = satname;
  else name = String(findSatelliteNameByNorad(satid));

  if (name == "Unknown" || name.length() == 0) {
    name = "SAT " + String(satid);
  }

  addSatFavorite(satid, name);
  loadSatFavorites();
  server.send(200, "text/html", buildHtml(false));
}

void handleRemoveFavorite() {
  long norad = satid;
  if (server.hasArg("norad")) {
    norad = server.arg("norad").toInt();
  }

  removeSatFavorite(norad);
  loadSatFavorites();
  server.send(200, "text/html", buildConfigHtml());
}

void handleConfigPage() {
  loadSatFavorites();
  refreshVisibleNow();
  refreshFutureVisibility(1, 10);  // 1 día, elevación mínima 10°
  server.send(200, "text/html", buildConfigHtml());
}

void handleRestoreFavorites() {
  initSatFavorites();
  loadSatFavorites();
  server.send(200, "text/html", buildConfigHtml());
}


void handleReloadFavorites() {
  loadSatFavorites();
  server.send(200, "text/html", buildConfigHtml());
}

void handleAddFavoriteCustom() {
  loadSatFavorites();

  if (!server.hasArg("norad")) {
    server.send(200, "text/html", buildConfigHtml());
    return;
  }

  long norad = server.arg("norad").toInt();
  String name = server.hasArg("name") ? server.arg("name") : "";

  if (norad <= 0) {
    server.send(200, "text/html", buildConfigHtml());
    return;
  }

  name.trim();
  if (name.length() == 0) {
    name = "SAT " + String(norad);
  }

  addSatFavorite(norad, name);
  loadSatFavorites();

  satid = norad;
  satname = name;

  server.send(200, "text/html", buildConfigHtml());
}

void handleSearchSat() {
  if (!server.hasArg("q")) {
    server.send(200, "text/html", buildConfigHtml());
    return;
  }

  String q = server.arg("q");
  searchSatellitesByName(q);
  server.send(200, "text/html", buildConfigHtml());
}

void handleAddFavoriteFromSearch() {
  Serial.println("=== handleAddFavoriteFromSearch ===");

  if (!server.hasArg("norad")) {
    Serial.println("Falta argumento norad");
    server.send(200, "text/html", buildConfigHtml());
    return;
  }

  long norad = server.arg("norad").toInt();
  String name = server.hasArg("name") ? server.arg("name") : "";

  Serial.print("norad recibido: ");
  Serial.println(norad);
  Serial.print("name recibido: ");
  Serial.println(name);

  if (norad <= 0) {
    Serial.println("NORAD invalido");
    server.send(200, "text/html", buildConfigHtml());
    return;
  }

  name.trim();
  if (name.length() == 0) {
    name = "SAT " + String(norad);
  }

  bool ok = addSatFavorite(norad, name);
  Serial.print("addSatFavorite: ");
  Serial.println(ok ? "OK" : "FALLO");

  bool loaded = loadSatFavorites();
  Serial.print("loadSatFavorites: ");
  Serial.println(loaded ? "OK" : "FALLO");

  satid = norad;
  satname = name;

  server.send(200, "text/html", buildConfigHtml());
}

void handleSearchVisibleNow() {
  searchVisibleNow();
  server.send(200, "text/html", buildConfigHtml());
}

void handleSelectVisibleSat() {
  if (!server.hasArg("norad")) {
    server.send(200, "text/html", buildConfigHtml());
    return;
  }

  satid = server.arg("norad").toInt();
  if (server.hasArg("name")) satname = server.arg("name");

  server.send(200, "text/html", buildConfigHtml());
}

void handleAddVisibleSatToFavorites() {
  if (!server.hasArg("norad")) {
    server.send(200, "text/html", buildConfigHtml());
    return;
  }

  long norad = server.arg("norad").toInt();
  String name = server.hasArg("name") ? server.arg("name") : "";

  if (norad > 0) {
    if (name.length() == 0) name = "SAT " + String(norad);
    addSatFavorite(norad, name);
    loadSatFavorites();
  }

  server.send(200, "text/html", buildConfigHtml());
}
