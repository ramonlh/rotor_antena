

#include <Arduino.h>
#include <Esp.h>
#include "web_handlers.h"
#include "app_state.h"
#include "config_motion.h"
#include "imu_sensor.h"
#include "network_services.h"
#include "sat_tracking.h"
#include "sat_fetch.h"
#include "rotor_io.h"
#include "rotor_positioning.h"
#include "sat_catalog.h"
#include "sat_favorites.h"
#include "sat_search.h"
#include "sat_visibility.h"
#include "sat_visible_now.h"
#include "sat_radio_info.h"
#include "diagnostic_log.h"
#include "web_assets.h"
#include "ntfy_notifications.h"

static void redirectConfig();

static void jsonString(String& out, const String& in) {
  out += '"';
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c != '\r' && (uint8_t)c >= 0x20) {
      out += c;
    }
  }
  out += '"';
}

static void jsonString(String& out, const char* in) {
  out += '"';
  if (in != nullptr) {
    for (const char* p = in; *p != '\0'; ++p) {
      const char c = *p;
      if (c == '"' || c == '\\') out += '\\';
      if (c == '\n') out += "\\n";
      else if (c != '\r' && (uint8_t)c >= 0x20) out += c;
    }
  }
  out += '"';
}

static const char* livePredictionText(bool targetValid) {
  if (isPositionsRequestInProgress()) return "Actualizando prediccion...";
  if (lastresponse == 200 && targetValid) return "Prediccion OK";
  if (lastresponse == -2) return "Sin prediccion N2YO";
  if (lastresponse == -3) return "JSON invalido N2YO";
  if (lastresponse == -1) return "Error conexion N2YO";
  return "Fallo prediccion";
}

static const char* liveOrientationText() {
  if (!orientMoveActive) return "IDLE";
  if (orientPhase == ORIENT_WAIT_ELEV) return "MOV_ELEV";
  if (orientPhase == ORIENT_WAIT_AZ) return "MOV_AZ";
  return "ORIENT";
}

static void sendInitJson() {
  stamp.getDateTime(unixtime);
  String j;
  j.reserve(520);
  j = "{\"dateTime\":\"";
  j += String(stamp.year); j += "/"; j += String(stamp.month); j += "/"; j += String(stamp.day);
  j += " - "; j += String(stamp.hour); j += ":";
  if (stamp.minute < 10) j += '0';
  j += String(stamp.minute); j += ":";
  if (stamp.second < 10) j += '0';
  j += String(stamp.second);
  j += "\",\"wifi\":";
  jsonString(j, getWifiStatusText());
  j += ",\"satId\":"; j += String(satid);
  j += ",\"satName\":"; jsonString(j, satname);
  j += ",\"radioSource\":"; jsonString(j, getRadioInfoSourceText());
  j += ",\"simulation\":"; j += simulationMode ? "true" : "false";
  j += ",\"azRef\":";
  j += (simulationMode || isImuAzimuthReferenceSet()) ? "true" : "false";
  j += "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

static void sendOptionsHtml() {
  String s;
  if (hasSatFavorites()) s = buildFavoriteOptionsHtml(satid);
  else s = buildSatelliteOptionsHtml(satid);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", s);
}

static void sendCalibrationJson() {
  String j;
  j.reserve(220);
  j = "{\"hasAzLeft\":";
  j += hasAzLeftLimitReference() ? "true" : "false";
  j += ",\"azLeft\":"; j += String(getAzLeftLimitReference(), 1);
  j += ",\"hasElLow\":"; j += hasElLowerLimitReference() ? "true" : "false";
  j += ",\"elLow\":"; j += String(getElLowerLimitReference(), 1);
  j += "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

static void sendConfigJson() {
  loadSatFavorites();
  String j;
  j.reserve(6500);
  j = "{\"favCount\":";
  j += String(getSatFavoritesCount());
  j += ",\"visStatus\":";
  String status;
  if (!hasVisibleNowSnapshot()) {
    status = isVisibleNowRefreshInProgress() ? "actualizando..." : "sin datos";
  } else {
    status = "cache de ";
    status += String(getVisibleNowAgeMs() / 1000UL);
    status += " s";
    if (isVisibleNowRefreshInProgress()) status += " (actualizando)";
  }
  jsonString(j, status);

  j += ",\"visible\":[";
  for (int i = 0; i < getVisibleNowCount(); ++i) {
    if (i) j += ',';
    const VisibleSatItem x = getVisibleNowItem(i);
    j += "{\"id\":"; j += String(x.noradId);
    j += ",\"name\":"; jsonString(j, x.name);
    j += '}';
  }

  j += "],\"favorites\":[";
  for (int i = 0; i < getSatFavoritesCount(); ++i) {
    if (i) j += ',';
    const SatFavoriteItem x = getSatFavorite(i);
    j += "{\"id\":"; j += String(x.noradId);
    j += ",\"name\":"; jsonString(j, x.name);
    j += ",\"state\":"; j += String((int)getVisibleNowState(x.noradId));
    const bool hasPass = hasFavoriteFuturePass(x.noradId);
    j += ",\"passTs\":";
    j += String(hasPass ? getFavoriteNextPassUTC(x.noradId) : 0UL);
    j += ",\"passEl\":";
    j += String(hasPass ? getFavoriteNextPassMaxEl(x.noradId) : 0.0f, 1);
    j += '}';
  }

  j += "],\"search\":[";
  for (int i = 0; i < getSatSearchResultCount(); ++i) {
    if (i) j += ',';
    const SatSearchItem x = getSatSearchResult(i);
    j += "{\"id\":"; j += String(x.noradId);
    j += ",\"name\":"; jsonString(j, x.name);
    j += '}';
  }
  j += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

void handleTestNtfy() {
  requestNtfyTestNotification();
  redirectConfig();
}

static void sendAmateurCatalogJson() {
  String j;

  const int n = getAmateurCatalogCount();

  // Unos ~100 elementos NORAD+nombre siguen siendo mucho menores
  // que el antiguo HTML completo construido en RAM.
  j.reserve(900 + n * 58);

  j = "{\"loaded\":";
  j += hasAmateurCatalog() ? "true" : "false";

  j += ",\"status\":";
  jsonString(j, String(getAmateurCatalogStatusText()));

  j += ",\"items\":[";

  for (int i = 0; i < n; ++i) {
    if (i) j += ',';

    const AmateurCatalogItem x =
        getAmateurCatalogItem(i);

    j += "{\"id\":";
    j += String(x.noradId);
    j += ",\"name\":";
    jsonString(j, x.name);
    j += '}';
  }

  j += "],\"inspection\":{";

  const SatRadioInspection r =
      getSatRadioInspection();

  j += "\"checked\":";
  j += r.checked ? "true" : "false";

  j += ",\"ok\":";
  j += r.queryOk ? "true" : "false";

  j += ",\"id\":";
  j += String(r.noradId);

  j += ",\"name\":";
  jsonString(j, r.name);

  j += ",\"txTotal\":";
  j += String(r.txTotal);

  j += ",\"txActive\":";
  j += String(r.txActive);

  j += ",\"txAmateur\":";
  j += String(r.txAmateur);

  j += ",\"txActiveAmateur\":";
  j += String(r.txActiveAmateur);

  j += ",\"txTwoWay\":";
  j += String(r.txTwoWay);

  j += ",\"modes\":";
  jsonString(j, r.modes);

  j += ",\"downlinks\":";
  jsonString(j, r.downlinks);

  j += ",\"uplinks\":";
  jsonString(j, r.uplinks);

  j += ",\"types\":";
  jsonString(j, r.types);

  j += ",\"functions\":";
  jsonString(j, r.functions);

  j += ",\"error\":";
  jsonString(j, r.error);

  j += "}}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}


void handleIndex() {
  bool satChanged = false;
  if (server.hasArg("SATS")) {
    const long newSat = server.arg("SATS").toInt();
    if (newSat > 0 && newSat != satid) {
      satid = newSat;
      satname = String(findSatelliteNameByNorad(newSat));
      if (satname == "Unknown" || satname.length() == 0) {
        satname = "SAT " + String(newSat);
      }
      resetPositionsStatus();
      tabunixtime[0] = 0;
      satazimut = 0.0f;
      satelevat = 0.0f;
      satChanged = true;
    }
  }
  // Tras enviar un formulario, redirigir siempre a un GET limpio. Algunos
  // navegadores conservan el contexto del POST y pueden mostrar el cuerpo
  // HTML como texto al recargar o restaurar la página.
  if (server.method() == HTTP_POST) {
    server.sendHeader("Location", "/", true);
    server.sendHeader("Cache-Control", "no-store");
    server.send(303, "text/plain", "");
    if (!isAnyMotionBusy() && (satChanged || tabunixtime[0] == 0)) {
      getPositions();
      if (satChanged) refreshRadioInfoForSat(satid);
    }
    return;
  }

  sendEmbeddedIndexHtml();
  if (!isAnyMotionBusy() && (satChanged || tabunixtime[0] == 0)) {
    getPositions();
    if (satChanged) refreshRadioInfoForSat(satid);
  }
}

void handleLive() {
  if (server.hasArg("options")) { sendOptionsHtml(); return; }
  if (server.hasArg("init")) { sendInitJson(); return; }
  if (server.hasArg("radio")) {
    if (server.hasArg("retry") &&
        !isAnyMotionBusy() &&
        !isPositionsRequestInProgress() &&
        !isN2yoRequestBusy()) {
      refreshRadioInfoForSat(satid);
    }
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html", buildRadioInfoHtml(satid));
    return;
  }
  if (server.hasArg("config")) { sendConfigJson(); return; }
  if (server.hasArg("amateur")) { sendAmateurCatalogJson(); return; }
  if (server.hasArg("cal")) { sendCalibrationJson(); return; }

  const bool includeFavorites = server.hasArg("favorites");
  if (includeFavorites) {
    requestVisibleNowRefreshIfStale(90000UL);

    // Dar prioridad a la consulta unica /above. Si ya ha terminado sin una
    // instantanea valida, activar el respaldo /positions para favoritos.
    if (!isVisibleNowRefreshInProgress()) {
      requestFavoriteVisibilityRefreshIfStale();
    }
  }

  const long targetIndex = (tabunixtime[0] != 0)
      ? long(unixtime) - long(tabunixtime[0]) : -1L;
  const bool targetValid =
      targetIndex >= 0 && targetIndex < long(kPredictionWindowSec);

  SatVisibleState visibleState = getVisibleNowState(satid);
  if (targetValid) {
    visibleState = (satelevat >= kMinElevationDeg)
        ? SAT_VIS_VISIBLE : SAT_VIS_NOT_VISIBLE;
  }

  // Reutilizar la capacidad entre sondeos (cada 0,5-1 s) evita reservar y
  // liberar el mismo bloque del heap continuamente.
  static String json;
  json = "";
  json.reserve(includeFavorites ? 2300 : 900);
  json = "{\"az\":"; json += String(angleZ, 1);
  json += ",\"el\":"; json += String(angleX, 1);
  json += ",\"satAz\":"; json += String(satazimut, 1);
  json += ",\"satEl\":"; json += String(satelevat, 1);
  json += ",\"satId\":"; json += String(satid);
  json += ",\"satName\":"; jsonString(json, satname);
  json += ",\"targetValid\":"; json += targetValid ? "true" : "false";
  json += ",\"simulation\":"; json += simulationMode ? "true" : "false";
  json += ",\"azRef\":";
  json += (simulationMode || isImuAzimuthReferenceSet()) ? "true" : "false";
  json += ",\"azCalFound\":"; json += isAzLeftCalibrationFound() ? "true" : "false";
  json += ",\"azReleaseDone\":";
  json += wasAzLeftLimitReleaseSuccessful() ? "true" : "false";
  json += ",\"elCalFound\":"; json += isElLowerCalibrationFound() ? "true" : "false";
  json += ",\"elRaw\":"; json += String(getImuRawElevationDeg(), 1);
  json += ",\"tracking\":"; json += enableseguirsat ? "true" : "false";
  json += ",\"visibleState\":"; json += String((int)visibleState);
  json += ",\"prediction\":"; jsonString(json, livePredictionText(targetValid));
  json += ",\"orientation\":"; jsonString(json, liveOrientationText());
  json += ",\"motion\":"; jsonString(json, getMotionStatusText());
  json += ",\"radioSource\":"; jsonString(json, getRadioInfoSourceText());
  json += ",\"radioHttpCode\":"; json += String(getLastRadioHttpCode());
  json += ",\"radioNoradId\":"; json += String(getRadioInfoNoradId());
  json += ",\"celestrakStatus\":";
  jsonString(json, getAmateurCatalogStatusText());
  json += ",\"celestrakCount\":";
  json += String(getAmateurCatalogCount());
  json += ",\"wifi\":"; jsonString(json, getWifiStatusText());
  json += ",\"heap\":"; json += String(ESP.getFreeHeap());
  json += ",\"minHeap\":"; json += String(ESP.getMinFreeHeap());
  json += ",\"maxBlock\":"; json += String(ESP.getMaxAllocHeap());
  json += ",\"uptimeSec\":"; json += String(millis() / 1000UL);
  json += ",\"imuHealthy\":"; json += isImuDataHealthy() ? "true" : "false";
  const uint32_t imuAge = getImuSampleAgeMs();
  json += ",\"imuAgeMs\":";
  json += imuAge == UINT32_MAX ? "-1" : String(imuAge);
  const uint32_t predictionAge = getLastPredictionAgeMs();
  json += ",\"predictionAgeSec\":";
  json += predictionAge == UINT32_MAX ? "-1" : String(predictionAge / 1000UL);
  json += ",\"predictionBusy\":";
  json += isPositionsRequestInProgress() ? "true" : "false";
  json += ",\"visibleBusy\":";
  json += isVisibleNowRefreshInProgress() ? "true" : "false";
  json += ",\"visibleSnapshot\":";
  json += hasVisibleNowSnapshot() ? "true" : "false";
  json += ",\"visibleAgeSec\":";
  json += hasVisibleNowSnapshot()
      ? String(getVisibleNowAgeMs() / 1000UL) : "-1";
  json += ",\"favoriteBusy\":";
  json += isFavoriteVisibilityRefreshInProgress() ? "true" : "false";
  json += ",\"favoriteCurrentId\":";
  json += String(getFavoriteVisibilityCurrentId());
  json += ",\"favoriteLastId\":";
  json += String(getFavoriteVisibilityLastId());
  json += ",\"favoriteLastState\":";
  json += String((int)getFavoriteVisibilityLastState());
  json += ",\"favoriteRetrySec\":";
  json += String((getFavoriteVisibilityRetryRemainingMs() + 999UL) / 1000UL);
  json += ",\"visibleRetrySec\":";
  json += String((getVisibleNowRetryRemainingMs() + 999UL) / 1000UL);
  json += ",\"visibleLastError\":";
  jsonString(json, getVisibleNowLastError());
  json += ",\"favoriteLastError\":";
  jsonString(json, getFavoriteVisibilityLastError());
  json += ",\"retrySec\":";
  json += String((getPositionsRetryRemainingMs() + 999UL) / 1000UL);
  json += ",\"lastNetworkError\":"; jsonString(json, getLastPredictionError());
  json += ",\"lastSafetyStop\":"; jsonString(json, getLastSafetyStopReason());
  json += ",\"events\":[";
  const int eventCount = getDiagnosticEventCount();
  for (int i = 0; i < eventCount; ++i) {
    uint32_t eventSec = 0;
    char eventText[72];
    if (!getDiagnosticEvent(i, eventSec, eventText, sizeof(eventText))) continue;
    if (i) json += ',';
    json += "{\"sec\":"; json += String(eventSec);
    json += ",\"text\":"; jsonString(json, String(eventText));
    json += '}';
  }
  json += ']';

  if (includeFavorites) {
    json += ",\"favoritesValid\":";
    // El array siempre es util: durante el respaldo va publicando los estados
    // que ya se han comprobado y conserva UNKNOWN para los pendientes.
    json += "true";
    json += ",\"favoritesRefreshing\":";
    json += (isVisibleNowRefreshInProgress() ||
             isFavoriteVisibilityRefreshInProgress()) ? "true" : "false";
    json += ",\"favorites\":[";
    for (int i = 0; i < getSatFavoritesCount(); ++i) {
      if (i) json += ',';
      const SatFavoriteItem item = getSatFavorite(i);
      const SatVisibleState itemState = getVisibleNowState(item.noradId);
      json += "{\"id\":"; json += String(item.noradId);
      json += ",\"state\":"; json += String((int)itemState);
      json += '}';
    }
    json += ']';
  }
  json += '}';
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send(200, "application/json", json);
}

static void redirectRoot() {
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void handleSeguirSat() {
  enableseguirsat = !enableseguirsat;

  if (enableseguirsat) {
    getPositions();

    // Si aún no hay datos, conservar la solicitud de seguimiento: la tarea
    // de red publicará la predicción y el tick empezará a mover después.
    if (tabunixtime[0] != 0 && satelevat < kMinElevationDeg) {
      enableseguirsat = false;
      stopAllMotion();
    }
  } else {
    stopAllMotion();
  }

  redirectRoot();
}

void handleIrAOrigen() {
  // Permite salir del final izquierdo aunque la referencia absoluta se haya
  // perdido. La propia maniobra esta limitada por distancia IMU y watchdog.
  if (server.hasArg("releaseaz")) {
    if (startAzLeftLimitRelease()) {
      server.send(204, "text/plain", "");
    } else if (simulationMode) {
      server.send(409, "text/plain", "Cambie el controlador a modo REAL");
    } else if (isRotorReferenceBusy()) {
      server.send(409, "text/plain", "Ya hay una maniobra en curso");
    } else {
      server.send(409, "text/plain", "No se pudo liberar el azimut");
    }
    return;
  }

  // Calibración inicial: buscar el final sin conocer todavía su valor absoluto.
  if (server.hasArg("calfindaz")) {
    if (startAzLeftCalibrationSearch()) {
      server.send(204, "text/plain", "");
    } else if (simulationMode) {
      server.send(409, "text/plain", "Cambie el controlador a modo REAL");
    } else if (isRotorReferenceBusy()) {
      server.send(409, "text/plain", "Ya hay una referencia en curso");
    } else if (isImuManualAzimuthMoving()) {
      server.send(409, "text/plain", "Hay movimiento manual de azimut");
    } else {
      server.send(409, "text/plain", "No se pudo preparar el rotor");
    }
    return;
  }

  if (server.hasArg("calfindel")) {
    if (startElLowerCalibrationSearch()) {
      server.send(204, "text/plain", "");
    } else if (simulationMode) {
      server.send(409, "text/plain", "Cambie el controlador a modo REAL");
    } else if (isRotorReferenceBusy()) {
      server.send(409, "text/plain", "Ya hay una referencia en curso");
    } else if (isImuManualAzimuthMoving()) {
      server.send(409, "text/plain", "Hay movimiento manual de azimut");
    } else {
      server.send(409, "text/plain", "No se pudo preparar el rotor");
    }
    return;
  }

  // Una vez detectado físicamente el final, aplicar el valor medido.
  if (server.hasArg("calsetaz")) {
    const float v = server.arg("calsetaz").toFloat();
    server.send(
        applyAzLeftCalibration(v) ? 204 : 409,
        "text/plain",
        ""
    );
    return;
  }

  if (server.hasArg("calsetel")) {
    const float v = server.arg("calsetel").toFloat();
    server.send(
        applyElLowerCalibration(v) ? 204 : 409,
        "text/plain",
        ""
    );
    return;
  }

  // Referenciado posterior usando valores ya guardados.
  if (server.hasArg("refaz")) {
    server.send(
        startAzLeftReference() ? 204 : 409,
        "text/plain",
        ""
    );
    return;
  }

  if (server.hasArg("refel")) {
    server.send(
        startElLowerReference() ? 204 : 409,
        "text/plain",
        ""
    );
    return;
  }

  // Compatibilidad con la antigua pantalla.
  if (server.hasArg("refsave")) {
    if (!server.hasArg("azleft") ||
        !server.hasArg("ellow") ||
        !saveRotorReferenceLimits(
            server.arg("azleft").toFloat(),
            server.arg("ellow").toFloat())) {
      server.send(409, "text/plain", "REF ERR");
      return;
    }
    server.send(204, "text/plain", "");
    return;
  }

  if (!simulationMode && !isImuAzimuthReferenceSet()) {
    server.send(409, "text/plain", "AZ sin referencia");
    return;
  }

  orientAntenna(0.0f, 0.0f);
  server.send(204, "text/plain", "");
}

void handleOrientarManual() {
  if (!server.hasArg("az") || !server.hasArg("el")) {
    server.send(400, "text/plain", "Faltan az/el");
    return;
  }

  int az = server.arg("az").toInt();
  int el = server.arg("el").toInt();

  if (az < 0) az = 0;
  if (az > 360) az = 360;
  if (el < 0) el = 0;
  if (el > 90) el = 90;

  if (!simulationMode && !isImuAzimuthReferenceSet()) {
    server.send(
        409,
        "text/plain",
        "AZ sin referencia"
    );
    return;
  }

  enableseguirsat = false;
  stopAllMotion();
  orientAntenna((float)az, (float)el);

  // Responder inmediatamente: no reconstruir toda la pagina mientras
  // el rotor acaba de iniciar una maniobra.
  server.send(204, "text/plain", "");
}

static float getJogStepFromRequest() {
  float step = 5.0f;

  if (server.hasArg("step")) {
    step = server.arg("step").toFloat();
  }

  // Protección del rotor frente a valores vacíos o excesivos.
  if (step < 0.5f) step = 0.5f;
  if (step > 45.0f) step = 45.0f;

  return step;
}

void handleArriba() {
  jogElevation(+getJogStepFromRequest());
  server.send(204, "text/plain", "");
}

void handleAbajo() {
  jogElevation(-getJogStepFromRequest());
  server.send(204, "text/plain", "");
}

void handleGiroIzda() {
  if (!simulationMode && !isImuAzimuthReferenceSet()) {
    server.send(409, "text/plain", "AZ sin referencia");
    return;
  }

  jogAzimuth(-getJogStepFromRequest());
  server.send(204, "text/plain", "");
}

void handleGiroDcha() {
  if (!simulationMode && !isImuAzimuthReferenceSet()) {
    server.send(409, "text/plain", "AZ sin referencia");
    return;
  }

  jogAzimuth(+getJogStepFromRequest());
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
  redirectRoot();
}

void handleNotFound() {
  server.send(404, "text/plain", "La pagina no existe");
}

void handleToggleSim() {
  // Detener cualquier movimiento antes de cambiar de modo: en simulación
  // los watchdogs reales no se ejecutan.
  stopAllMotion();
  simulationMode = !simulationMode;
  redirectRoot();
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
  redirectRoot();
}

static void redirectConfig() {
  server.sendHeader("Location", "/config");
  server.send(303, "text/plain", "");
}

void handleRemoveFavorite() {
  long norad = satid;
  if (server.hasArg("norad")) {
    norad = server.arg("norad").toInt();
  }

  removeSatFavorite(norad);
  loadSatFavorites();
  redirectConfig();
}

void handleConfigPage() {
  if (server.hasArg("cal")) {
    sendEmbeddedCalibrationHtml();
    return;
  }
  loadSatFavorites();
  requestVisibleNowRefreshIfStale(90000UL);
  sendEmbeddedConfigHtml();
}

void handleRestoreFavorites() {
  initSatFavorites();
  loadSatFavorites();
  redirectConfig();
}


void handleReloadFavorites() {
  loadSatFavorites();
  redirectConfig();
}

void handleAddFavoriteCustom() {
  loadSatFavorites();

  if (!server.hasArg("norad")) {
    redirectConfig();
    return;
  }

  long norad = server.arg("norad").toInt();
  String name = server.hasArg("name") ? server.arg("name") : "";

  if (norad <= 0) {
    redirectConfig();
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
  resetPositionsStatus();

  redirectConfig();
}

void handleLoadAmateurCatalog() {
  // El botón normal carga RAM/FFat. Sólo irá a Internet si todavía
  // no existe ninguna caché local.
  if (!isAnyMotionBusy()) {
    loadAmateurRadioCatalog();
  }

  redirectConfig();
}

void handleRefreshAmateurCatalog() {
  // Actualización remota explícita. sat_search.cpp impone además
  // un mínimo de 2 horas entre intentos en la misma sesión.
  if (!isAnyMotionBusy()) {
    refreshAmateurRadioCatalog();
  }

  redirectConfig();
}

void handleInspectSatellite() {
  if (!server.hasArg("norad")) {
    redirectConfig();
    return;
  }

  const long norad =
      server.arg("norad").toInt();

  String name =
      server.hasArg("name")
          ? server.arg("name")
          : "";

  name.trim();

  if (norad <= 0) {
    redirectConfig();
    return;
  }

  if (name.length() == 0) {
    name = "SAT " + String(norad);
  }

  if (!isAnyMotionBusy()) {
    inspectSatelliteRadio(norad, name);
  }

  redirectConfig();
}


void handleSearchSat() {
  if (!server.hasArg("q")) {
    redirectConfig();
    return;
  }

  String q = server.arg("q");
  searchSatellitesByName(q);
  redirectConfig();
}

void handleAddFavoriteFromSearch() {
  if (!server.hasArg("norad")) {    redirectConfig();
    return;
  }

  long norad = server.arg("norad").toInt();
  String name = server.hasArg("name") ? server.arg("name") : "";
  if (norad <= 0) {    redirectConfig();
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
  resetPositionsStatus();

  redirectConfig();
}

void handleSearchVisibleNow() {
  // Forzar refresco, pero en segundo plano.
  requestVisibleNowRefreshIfStale(0UL);
  redirectConfig();
}

void handleSelectVisibleSat() {
  if (!server.hasArg("norad")) {
    redirectConfig();
    return;
  }

  satid = server.arg("norad").toInt();
  if (server.hasArg("name")) satname = server.arg("name");
  resetPositionsStatus();

  // Todavía no existe una predicción /positions para este satélite.
  // Al volver a "/", handleIndex() la solicitará después de enviar la página.
  tabunixtime[0] = 0;
  satazimut = 0.0f;
  satelevat = 0.0f;

  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void handleAddVisibleSatToFavorites() {
  if (!server.hasArg("norad")) {
    redirectConfig();
    return;
  }

  long norad = server.arg("norad").toInt();
  String name = server.hasArg("name") ? server.arg("name") : "";

  if (norad > 0) {
    if (name.length() == 0) name = "SAT " + String(norad);
    addSatFavorite(norad, name);
    loadSatFavorites();
  }

  redirectConfig();
}
