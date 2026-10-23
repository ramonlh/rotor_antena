#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include "sat_visible_now.h"
#include "sat_fetch.h"
#include "app_state.h"
#include "config.h"
#include "config_motion.h"
#include "rotor_positioning.h"
#include "sat_favorites.h"
#include "http_buffer.h"
#include "ntfy_notifications.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static constexpr int kVisibleDisplayMax = 32;

static const char* findKeyBefore(
    const char* start,
    const char* limit,
    const char* key) {
  const char* found = strstr(start, key);
  return found != nullptr && (limit == nullptr || found < limit)
      ? found : nullptr;
}

static bool readJsonNumber(
    const char* object,
    const char* objectEnd,
    const char* key,
    double& value) {
  const char* field = findKeyBefore(object, objectEnd, key);
  const char* colon = field != nullptr ? strchr(field, ':') : nullptr;
  if (colon == nullptr || colon >= objectEnd) return false;
  char* end = nullptr;
  value = strtod(colon + 1, &end);
  return end != colon + 1 && end <= objectEnd;
}

static bool readJsonString(
    const char* object,
    const char* objectEnd,
    const char* key,
    String& value) {
  const char* field = findKeyBefore(object, objectEnd, key);
  const char* colon = field != nullptr ? strchr(field, ':') : nullptr;
  const char* quote = colon != nullptr ? strchr(colon, '\"') : nullptr;
  if (quote == nullptr || quote >= objectEnd) return false;

  value = "";
  for (const char* p = quote + 1; p < objectEnd; ++p) {
    if (*p == '\"') return true;
    if (*p == '\\' && p + 1 < objectEnd) {
      ++p;
      if (*p == 'n') value += '\n';
      else if (*p == 'r') value += '\r';
      else if (*p == 't') value += '\t';
      else value += *p;
    } else {
      value += *p;
    }
  }
  value = "";
  return false;
}

// Se guardan muchos más NORAD que nombres.
// Así un favorito no queda marcado como NO sólo por estar más allá
// de los primeros 32 elementos devueltos por N2YO.
static constexpr int kVisibleIdMax = 256;

// Un pase típico puede durar unos 10 minutos. Un minuto limita el retraso del
// aviso sin abrir una conexión por cada favorito: /above los resuelve juntos.
static constexpr uint32_t kAutoRefreshAgeMs = 60UL * 1000UL;
static constexpr uint32_t kAutoServiceTickMs = 10000UL;

// Si N2YO rechaza /above por cuota, NO volver a martillear la API.
// El límite se expresa "per hour", por lo que esperamos algo más de una hora.
static constexpr uint32_t kQuotaBackoffMs = 65UL * 60UL * 1000UL;

// Para fallos HTTP/JSON genéricos basta un reintento prudente.
static constexpr uint32_t kGenericBackoffMs = 60UL * 1000UL;

// Incluso al pulsar manualmente "Actualizar visibles" no permitir
// dos peticiones /above con menos de 60 s de separación.
static constexpr uint32_t kManualMinRequestMs = 60UL * 1000UL;

static uint32_t gLastAboveAttemptMs = 0;

static uint32_t gNextAboveRequestMs = 0;
static bool gAboveQuotaBlocked = false;

static bool aboveBackoffActive() {
  if (gNextAboveRequestMs == 0) return false;
  return (int32_t)(gNextAboveRequestMs - millis()) > 0;
}

static void setAboveBackoff(
    uint32_t delayMs,
    bool quotaBlocked) {

  gNextAboveRequestMs = millis() + delayMs;
  gAboveQuotaBlocked = quotaBlocked;
}

static void clearAboveBackoff() {
  gNextAboveRequestMs = 0;
  gAboveQuotaBlocked = false;
}


static VisibleSatItem gVisible[kVisibleDisplayMax];
static int gVisibleCount = 0;

static long gVisibleIds[kVisibleIdMax];
static int gVisibleIdCount = 0;
static bool gVisibleIdOverflow = false;

// La última lista válida se conserva si N2YO falla.
static bool gHasSnapshot = false;
static uint32_t gLastRefreshMs = 0;
static String gVisibleLastError = "Sin consultas";
static String gFavoriteLastError = "Sin consultas";

// Una consulta HTTP como máximo.
static volatile bool gRefreshInProgress = false;
static TaskHandle_t gRefreshTaskHandle = nullptr;
static uint32_t gLastAutoServiceMs = 0;

static SemaphoreHandle_t gVisibleMutex = nullptr;
static portMUX_TYPE gRefreshMux = portMUX_INITIALIZER_UNLOCKED;

static void ensureVisibleMutex() {
  if (gVisibleMutex == nullptr) {
    gVisibleMutex = xSemaphoreCreateMutex();
  }
}

static void lockVisible() {
  ensureVisibleMutex();
  if (gVisibleMutex != nullptr) {
    xSemaphoreTake(gVisibleMutex, portMAX_DELAY);
  }
}

static void unlockVisible() {
  if (gVisibleMutex != nullptr) {
    xSemaphoreGive(gVisibleMutex);
  }
}

static String n2yoConnectionDetail(int code) {
  String detail = "HTTP ";
  detail += String(code);
  if (code < 0) {
    detail += " (";
    detail += HTTPClient::errorToString(code);
    detail += ")";
  }
  IPAddress resolved;
  if (WiFi.hostByName("api.n2yo.com", resolved)) {
    detail += "; DNS ";
    detail += resolved.toString();
    WiFiClient probe;
    probe.setTimeout(2000);
    if (probe.connect(resolved, 443)) {
      detail += "; TCP 443 OK";
      probe.stop();
    } else {
      detail += "; TCP 443 rechazado";
    }
  } else {
    detail += "; DNS fallido";
  }
  return detail;
}

static void setVisibleError(const String& value) {
  lockVisible();
  gVisibleLastError = value;
  unlockVisible();
}

static void setFavoriteError(const String& value) {
  lockVisible();
  gFavoriteLastError = value;
  unlockVisible();
}


// -----------------------------------------------------------------------------
// VISIBILIDAD DE FAVORITOS POR /positions
// -----------------------------------------------------------------------------
// /above queda reservado para descubrir "Visibles ahora".
// Los favoritos se comprueban de forma independiente con /positions/.../1,
// que devuelve un JSON pequeño y usa el límite mucho mayor de "positions".
static constexpr int kFavoriteVisMax = 32;
static constexpr uint32_t kFavoriteRefreshMs =
    10UL * 60UL * 1000UL;
static constexpr uint32_t kFavoriteStateMaxAgeMs =
    12UL * 60UL * 1000UL;

struct FavoriteVisState {
  long noradId;
  SatVisibleState state;
  uint32_t checkedMs;
};

static FavoriteVisState gFavoriteVis[kFavoriteVisMax];
static volatile bool gFavoriteRefreshInProgress = false;
static TaskHandle_t gFavoriteRefreshTaskHandle = nullptr;
static portMUX_TYPE gFavoriteRefreshMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t gLastFavoriteRefreshMs = 0;
static uint32_t gNextFavoriteAttemptMs = 0;
static bool gFavoriteFailurePaused = false;
static volatile long gFavoriteCurrentId = 0;
static volatile long gFavoriteLastId = 0;
static volatile SatVisibleState gFavoriteLastState = SAT_VIS_UNKNOWN;
static constexpr uint32_t kFavoriteStepIntervalMs = 15UL * 1000UL;
static constexpr uint32_t kFavoriteFailureBackoffMs =
    3UL * 60UL * 1000UL;

static int findFavoriteVisIndex(long noradId) {
  for (int i = 0; i < kFavoriteVisMax; ++i) {
    if (gFavoriteVis[i].noradId == noradId) return i;
  }
  return -1;
}

static int ensureFavoriteVisIndex(long noradId) {
  int i = findFavoriteVisIndex(noradId);
  if (i >= 0) return i;

  for (i = 0; i < kFavoriteVisMax; ++i) {
    if (gFavoriteVis[i].noradId == 0) {
      gFavoriteVis[i].noradId = noradId;
      gFavoriteVis[i].state = SAT_VIS_UNKNOWN;
      gFavoriteVis[i].checkedMs = 0;
      return i;
    }
  }
  return -1;
}

static void setFavoriteVisState(
    long noradId,
    SatVisibleState state) {

  lockVisible();
  const int i = ensureFavoriteVisIndex(noradId);
  if (i >= 0) {
    gFavoriteVis[i].state = state;
    gFavoriteVis[i].checkedMs = millis();
  }
  unlockVisible();
}

static SatVisibleState getFreshFavoriteVisState(long noradId) {
  SatVisibleState result = SAT_VIS_UNKNOWN;

  lockVisible();
  const int i = findFavoriteVisIndex(noradId);
  if (i >= 0 &&
      gFavoriteVis[i].checkedMs != 0 &&
      (millis() - gFavoriteVis[i].checkedMs) <
          kFavoriteStateMaxAgeMs) {
    result = gFavoriteVis[i].state;
  }
  unlockVisible();

  return result;
}

static bool selectedPredictionValid() {
  if (tabunixtime[0] == 0) return false;

  const long targetIndex =
      long(unixtime) - long(tabunixtime[0]);

  return targetIndex >= 0 &&
         targetIndex < long(kPredictionWindowSec);
}

static bool readFavoriteCurrentVisibility(
    long noradId,
    SatVisibleState& stateOut) {

  if (noradId <= 0 ||
      WiFi.status() != WL_CONNECTED) {
    return false;
  }

  if (!tryBeginN2yoRequest()) {
    return false;
  }

  char url[256];
  snprintf(url, sizeof(url), "%s%ld/%.6f/%.6f/%d/1/&apiKey=%s",
           SERVER_POS_BASE, noradId, MY_LAT, MY_LON, MY_ALT_M, N2YO_API_KEY);

  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  secureClient.setHandshakeTimeout(10);
  HTTPClient http;
  http.begin(secureClient, url);
  http.useHTTP10(true);
  http.setConnectTimeout(3000);
  http.setTimeout(12000);

  const int code = http.GET();

  if (code != HTTP_CODE_OK) {
    char tlsText[96] = "";
    const int tlsCode = secureClient.lastError(tlsText, sizeof(tlsText));
    String detail = n2yoConnectionDetail(code);
    if (tlsCode < 0) {
      detail += "; TLS ";
      detail += String(tlsCode);
      detail += " ";
      detail += tlsText;
    }
    setFavoriteError(detail);
    http.end();
    endN2yoRequest();
    return false;
  }

  const char* payload = nullptr;
  size_t payloadLength = 0;
  const bool bodyOk = readHttpBody(http, payload, payloadLength);
  http.end();
  if (!bodyOk) {
    setFavoriteError("Respuesta vacia o mayor que el buffer HTTP");
    endN2yoRequest();
    return false;
  }
  const char* json = payload;
  const char* positions = strstr(json, "\"positions\"");
  const char* object = positions != nullptr ? strchr(positions, '{') : nullptr;
  const char* objectEnd = object != nullptr ? strchr(object, '}') : nullptr;
  double elevation = 0.0;
  if (object == nullptr || objectEnd == nullptr ||
      !readJsonNumber(object, objectEnd, "\"elevation\"", elevation)) {
    setFavoriteError("Respuesta JSON sin elevation");
    endN2yoRequest();
    return false;
  }

  stateOut =
      (float(elevation) >= kMinElevationDeg)
          ? SAT_VIS_VISIBLE
          : SAT_VIS_NOT_VISIBLE;

  setFavoriteError("Sin errores");
  endN2yoRequest();
  return true;
}

static void favoriteVisibilityTask(void* parameter) {
  (void)parameter;

  const int count = getSatFavoritesCount();

  Serial.print("Favoritos: comprobando visibilidad ");
  Serial.print(count);
  Serial.println(" satelites");

  // Como maximo se abre una conexion por ejecucion. El refresco de la
  // prediccion seleccionada tiene siempre prioridad sobre este respaldo.
  for (int i = 0; i < count; ++i) {
    if (isAnyMotionBusy() ||
        isPositionsRequestInProgress() ||
        !selectedPredictionValid()) break;

    const SatFavoriteItem item = getSatFavorite(i);
    if (item.noradId <= 0) continue;

    // El seleccionado ya tiene una predicción mucho más completa.
    if (item.noradId == satid &&
        selectedPredictionValid()) {
      setFavoriteVisState(
          item.noradId,
          (satelevat >= kMinElevationDeg)
              ? SAT_VIS_VISIBLE
              : SAT_VIS_NOT_VISIBLE
      );
      continue;
    }

    // Los estados recientes no necesitan otra conexion.
    if (getFreshFavoriteVisState(item.noradId) !=
        SAT_VIS_UNKNOWN) {
      continue;
    }

    SatVisibleState state = SAT_VIS_UNKNOWN;
    gFavoriteCurrentId = item.noradId;
    const bool ok = readFavoriteCurrentVisibility(
        item.noradId,
        state
    );

    if (ok) {
      setFavoriteVisState(item.noradId, state);
      gFavoriteLastId = item.noradId;
      gFavoriteLastState = state;
      gFavoriteFailurePaused = false;
      gNextFavoriteAttemptMs =
          millis() + kFavoriteStepIntervalMs;

      Serial.print("Fav ");
      Serial.print(item.noradId);
      Serial.print(": ");
      Serial.println(
          state == SAT_VIS_VISIBLE
              ? "VISIBLE"
              : "NO"
      );
    } else {
      // No insistir: una conexion fallida suele indicar falta de red,
      // rechazo remoto o poca memoria disponible para TLS.
      gNextFavoriteAttemptMs =
          millis() + kFavoriteFailureBackoffMs;
      gFavoriteFailurePaused = true;
      Serial.println(
          "Favoritos: fallo de conexion; pausa de 3 min"
      );
    }

    break;
  }

  gFavoriteCurrentId = 0;

  gLastFavoriteRefreshMs = millis();

  portENTER_CRITICAL(&gFavoriteRefreshMux);
  gFavoriteRefreshInProgress = false;
  gFavoriteRefreshTaskHandle = nullptr;
  portEXIT_CRITICAL(&gFavoriteRefreshMux);

  vTaskDelete(nullptr);
}

bool requestFavoriteVisibilityRefreshIfStale() {
  if (WiFi.status() != WL_CONNECTED ||
      isAnyMotionBusy() ||
      isPositionsRequestInProgress() ||
      !selectedPredictionValid()) {
    return true;
  }

  if (gNextFavoriteAttemptMs != 0 &&
      (int32_t)(gNextFavoriteAttemptMs - millis()) > 0) {
    return true;
  }
  gFavoriteFailurePaused = false;

  bool anyUnknown = false;
  const int count = getSatFavoritesCount();

  for (int i = 0; i < count; ++i) {
    const SatFavoriteItem item = getSatFavorite(i);
    if (getVisibleNowState(item.noradId) ==
        SAT_VIS_UNKNOWN) {
      anyUnknown = true;
      break;
    }
  }

  // Una instantánea /above completa ya clasifica todos los favoritos. No
  // abrir conexiones /positions/1 de respaldo mientras no haya desconocidos.
  if (!anyUnknown) {
    return true;
  }

  portENTER_CRITICAL(&gFavoriteRefreshMux);

  if (gFavoriteRefreshInProgress) {
    portEXIT_CRITICAL(&gFavoriteRefreshMux);
    return true;
  }

  gFavoriteRefreshInProgress = true;

  portEXIT_CRITICAL(&gFavoriteRefreshMux);

  const BaseType_t ok = xTaskCreate(
      favoriteVisibilityTask,
      "fav_vis",
      6144,
      nullptr,
      1,
      &gFavoriteRefreshTaskHandle
  );

  if (ok != pdPASS) {
    portENTER_CRITICAL(&gFavoriteRefreshMux);
    gFavoriteRefreshInProgress = false;
    gFavoriteRefreshTaskHandle = nullptr;
    portEXIT_CRITICAL(&gFavoriteRefreshMux);

    Serial.println(
        "No se pudo crear tarea fav_vis"
    );
    return false;
  }

  return true;
}

bool isFavoriteVisibilityRefreshInProgress() {
  portENTER_CRITICAL(&gFavoriteRefreshMux);
  const bool value = gFavoriteRefreshInProgress;
  portEXIT_CRITICAL(&gFavoriteRefreshMux);
  if (value) return true;

  // Durante una pausa por fallo no mantener al navegador sondeando cada 5 s;
  // el intervalo normal de la pagina reanudara el proceso mas adelante.
  if (gFavoriteFailurePaused &&
      gNextFavoriteAttemptMs != 0 &&
      (int32_t)(gNextFavoriteAttemptMs - millis()) > 0) {
    return false;
  }

  if (!selectedPredictionValid() ||
      isAnyMotionBusy() ||
      isPositionsRequestInProgress()) {
    return false;
  }

  // Indicar que el proceso sigue pendiente también durante los 15 s entre
  // conexiones. Así la interfaz solicita el siguiente paso automáticamente.
  const int count = getSatFavoritesCount();
  for (int i = 0; i < count; ++i) {
    const SatFavoriteItem item = getSatFavorite(i);
    if (item.noradId > 0 &&
        getFreshFavoriteVisState(item.noradId) ==
            SAT_VIS_UNKNOWN) {
      return true;
    }
  }

  return false;
}

long getFavoriteVisibilityCurrentId() {
  return gFavoriteCurrentId;
}

long getFavoriteVisibilityLastId() {
  return gFavoriteLastId;
}

SatVisibleState getFavoriteVisibilityLastState() {
  return gFavoriteLastState;
}

uint32_t getFavoriteVisibilityRetryRemainingMs() {
  if (gNextFavoriteAttemptMs == 0) return 0;
  const int32_t remaining =
      (int32_t)(gNextFavoriteAttemptMs - millis());
  return remaining > 0 ? (uint32_t)remaining : 0;
}

uint32_t getVisibleNowRetryRemainingMs() {
  if (gNextAboveRequestMs == 0) return 0;
  const int32_t remaining =
      (int32_t)(gNextAboveRequestMs - millis());
  return remaining > 0 ? (uint32_t)remaining : 0;
}

String getVisibleNowLastError() {
  lockVisible();
  const String result = gVisibleLastError;
  unlockVisible();
  return result;
}

String getFavoriteVisibilityLastError() {
  lockVisible();
  const String result = gFavoriteLastError;
  unlockVisible();
  return result;
}

void clearVisibleNow() {
  lockVisible();

  gVisibleCount = 0;
  for (int i = 0; i < kVisibleDisplayMax; ++i) {
    gVisible[i].noradId = 0;
    gVisible[i].name = "";
  }

  gVisibleIdCount = 0;
  for (int i = 0; i < kVisibleIdMax; ++i) {
    gVisibleIds[i] = 0;
  }

  gVisibleIdOverflow = false;
  gHasSnapshot = false;
  gLastRefreshMs = 0;

  unlockVisible();
}

int getVisibleNowCount() {
  lockVisible();
  const int count = gVisibleCount;
  unlockVisible();
  return count;
}

VisibleSatItem getVisibleNowItem(int index) {
  VisibleSatItem result{0, ""};

  lockVisible();
  if (index >= 0 && index < gVisibleCount) {
    result = gVisible[index];
  }
  unlockVisible();

  return result;
}

bool hasVisibleNowSnapshot() {
  lockVisible();
  const bool value = gHasSnapshot;
  unlockVisible();
  return value;
}

uint32_t getVisibleNowAgeMs() {
  lockVisible();
  const bool hasSnapshot = gHasSnapshot;
  const uint32_t lastRefresh = gLastRefreshMs;
  unlockVisible();

  if (!hasSnapshot) return UINT32_MAX;
  return millis() - lastRefresh;
}

bool isVisibleNowRefreshInProgress() {
  portENTER_CRITICAL(&gRefreshMux);
  const bool value = gRefreshInProgress;
  portEXIT_CRITICAL(&gRefreshMux);
  return value;
}

bool searchVisibleNow() {
  if (WiFi.status() != WL_CONNECTED) {
    setVisibleError("WiFi no conectada");
    Serial.println("searchVisibleNow: WiFi no conectada");
    return false;
  }

  // /positions tiene prioridad mientras su tarea esta trabajando, pero
  // /above puede mantener la visibilidad global aunque la prediccion del
  // satelite seleccionado este temporalmente sin datos.
  if (isPositionsRequestInProgress()) {
    return false;
  }

  // Si N2YO ya nos indicó límite horario, esperar en silencio.
  if (aboveBackoffActive()) {
    return false;
  }

  // /positions y /above nunca deben solaparse.
  if (!tryBeginN2yoRequest()) {
    return false;
  }

  gLastAboveAttemptMs = millis();

  // N2YO mide search_radius desde el cenit:
  // 90 = horizonte (EL >= 0). Nuestro rotor sólo trabaja desde
  // kMinElevationDeg, así que el radio es 90 - elevación mínima.
  int searchRadius =
      90 - (int)ceilf(kMinElevationDeg);
  if (searchRadius < 0) searchRadius = 0;
  if (searchRadius > 90) searchRadius = 90;

  char url[256];
  snprintf(url, sizeof(url),
           "https://api.n2yo.com/rest/v1/satellite/above/%.6f/%.6f/%d/%d/18/&apiKey=%s",
           MY_LAT, MY_LON, MY_ALT_M, searchRadius, N2YO_API_KEY);

  Serial.println("=== searchVisibleNow ===");

  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  secureClient.setHandshakeTimeout(10);
  HTTPClient http;
  http.begin(secureClient, url);
  http.useHTTP10(true);
  http.setConnectTimeout(3000);
  http.setTimeout(12000);

  const int code = http.GET();

  Serial.print("HTTP code above: ");
  Serial.println(code);

  if (code != HTTP_CODE_OK) {
    char tlsText[96] = "";
    const int tlsCode = secureClient.lastError(tlsText, sizeof(tlsText));
    String detail = n2yoConnectionDetail(code);
    if (tlsCode < 0) {
      detail += "; TLS ";
      detail += String(tlsCode);
      detail += " ";
      detail += tlsText;
    }
    setVisibleError(detail);
    http.end();
    endN2yoRequest();

    setAboveBackoff(
        kGenericBackoffMs,
        false
    );

    return false;
  }

  const char* payload = nullptr;
  size_t payloadLength = 0;
  const bool bodyOk = readHttpBody(http, payload, payloadLength);
  http.end();
  if (!bodyOk) {
    setVisibleError("Respuesta vacia o mayor que el buffer HTTP");
    endN2yoRequest();
    setAboveBackoff(kGenericBackoffMs, false);
    Serial.println("Visible: respuesta vacia o supera buffer HTTP");
    return false;
  }

  const char* json = payload;
  while (*json == ' ' || *json == '\r' || *json == '\n' || *json == '\t') ++json;
  if (*json != '{') {
    setVisibleError("Respuesta no JSON");
    Serial.println("Visible: JSON invalido");
    Serial.println(String(payload).substring(0, 180));

    endN2yoRequest();

    setAboveBackoff(
        kGenericBackoffMs,
        false
    );

    return false;
  }

  // N2YO puede devolver HTTP 200 y un objeto {"error":"..."}.
  // Eso NO es un JSON inválido; es una respuesta de error de la API.
  const char* aboveKey = strstr(json, "\"above\"");
  const char* errorKey = strstr(json, "\"error\"");
  if (errorKey != nullptr && (aboveKey == nullptr || errorKey < aboveKey)) {
    const char* rootEnd = strrchr(json, '}');
    String errorText;
    if (!readJsonString(errorKey, rootEnd, "\"error\"", errorText)) {
      errorText = "error de API";
    }

    Serial.print("N2YO /above: ");
    Serial.println(errorText);
    setVisibleError("API: " + errorText);

    const String errorLower =
        String(errorText);

    String lower = errorLower;
    lower.toLowerCase();

    if (lower.indexOf("exceeded") >= 0 &&
        lower.indexOf("transactions") >= 0) {

      setAboveBackoff(
          kQuotaBackoffMs,
          true
      );

      Serial.println(
          "N2YO /above: cuota agotada; "
          "reintento aplazado 65 min"
      );
    } else {
      setAboveBackoff(
          kGenericBackoffMs,
          false
      );
    }

    endN2yoRequest();
    return false;
  }

  const char* arrayStart = aboveKey != nullptr ? strchr(aboveKey, '[') : nullptr;
  const char* arrayEnd = arrayStart != nullptr ? strrchr(arrayStart, ']') : nullptr;
  if (arrayStart == nullptr || arrayEnd == nullptr) {
    setVisibleError("Respuesta JSON sin array above");
    Serial.println("Visible: respuesta sin array above");
    Serial.println(String(payload).substring(0, 180));

    endN2yoRequest();

    setAboveBackoff(
        kGenericBackoffMs,
        false
    );

    return false;
  }

  // Se analiza TODA la respuesta.
  VisibleSatItem nextVisible[kVisibleDisplayMax];
  long nextIds[kVisibleIdMax];

  int nextVisibleCount = 0;
  int nextIdCount = 0;
  bool nextOverflow = false;

  int n = 0;
  const char* cursor = arrayStart + 1;
  while (cursor < arrayEnd) {
    const char* object = strchr(cursor, '{');
    if (object == nullptr || object >= arrayEnd) break;
    const char* objectEnd = strchr(object, '}');
    if (objectEnd == nullptr || objectEnd > arrayEnd) break;
    cursor = objectEnd + 1;
    ++n;

    double idValue = 0.0;
    if (!readJsonNumber(object, objectEnd, "\"satid\"", idValue)) continue;
    const long norad = long(idValue);
    if (norad <= 0) continue;

    if (nextIdCount < kVisibleIdMax) {
      nextIds[nextIdCount++] = norad;
    } else {
      nextOverflow = true;
    }

    // Los nombres sólo son necesarios para la tabla "Visibles ahora".
    if (nextVisibleCount < kVisibleDisplayMax) {
      String name;
      if (!readJsonString(object, objectEnd, "\"satname\"", name)) continue;
      nextVisible[nextVisibleCount].noradId = norad;
      nextVisible[nextVisibleCount].name = name;
      ++nextVisibleCount;
    }
  }

  // Sólo el intercambio de la caché está protegido.
  lockVisible();

  gVisibleCount = nextVisibleCount;
  for (int i = 0; i < kVisibleDisplayMax; ++i) {
    if (i < nextVisibleCount) {
      gVisible[i] = nextVisible[i];
    } else {
      gVisible[i].noradId = 0;
      gVisible[i].name = "";
    }
  }

  gVisibleIdCount = nextIdCount;
  for (int i = 0; i < kVisibleIdMax; ++i) {
    gVisibleIds[i] = (i < nextIdCount) ? nextIds[i] : 0;
  }

  gVisibleIdOverflow = nextOverflow;
  gHasSnapshot = true;
  gLastRefreshMs = millis();

  unlockVisible();

  // Comparar únicamente después de publicar una instantánea completa válida.
  observeFavoriteVisibilitySnapshot();

  clearAboveBackoff();
  setVisibleError("Sin errores");
  endN2yoRequest();

  Serial.print("Visibles N2YO: ");
  Serial.print(n);
  Serial.print(" | IDs cache: ");
  Serial.print(nextIdCount);
  Serial.print(" | mostrados: ");
  Serial.println(nextVisibleCount);

  if (nextOverflow) {
    Serial.println(
        "AVISO: lista visible supera cache de IDs; "
        "los no encontrados se marcaran COMPROBANDO, nunca NO."
    );
  }

  // Una respuesta válida con cero visibles también es válida.
  return true;
}

bool refreshVisibleNowIfStale(uint32_t maxAgeMs) {
  // Llamadas automáticas nunca refrescan más rápido de un minuto.
  // maxAgeMs == 0 se considera petición manual/forzada.
  const bool force = (maxAgeMs == 0);

  uint32_t effectiveAgeMs = maxAgeMs;
  if (!force && effectiveAgeMs < kAutoRefreshAgeMs) {
    effectiveAgeMs = kAutoRefreshAgeMs;
  }

  if (hasVisibleNowSnapshot() &&
      getVisibleNowAgeMs() < effectiveAgeMs) {
    return true;
  }

  if (aboveBackoffActive() ||
      isPositionsRequestInProgress() ||
      isAnyMotionBusy()) {
    return true;
  }

  if (force &&
      gLastAboveAttemptMs != 0 &&
      (millis() - gLastAboveAttemptMs) <
          kManualMinRequestMs) {
    Serial.println(
        "N2YO /above: actualización manual ignorada "
        "(mínimo 60 s)"
    );
    return true;
  }

  return searchVisibleNow();
}


static void visibleRefreshTask(void* parameter) {
  (void)parameter;

  searchVisibleNow();

  portENTER_CRITICAL(&gRefreshMux);
  gRefreshInProgress = false;
  gRefreshTaskHandle = nullptr;
  portEXIT_CRITICAL(&gRefreshMux);

  vTaskDelete(nullptr);
}

bool requestVisibleNowRefreshIfStale(uint32_t maxAgeMs) {
  // maxAgeMs==0 sólo se usa para el botón manual.
  const bool force = (maxAgeMs == 0);

  uint32_t effectiveAgeMs = maxAgeMs;
  if (!force && effectiveAgeMs < kAutoRefreshAgeMs) {
    effectiveAgeMs = kAutoRefreshAgeMs;
  }

  if (hasVisibleNowSnapshot() &&
      getVisibleNowAgeMs() < effectiveAgeMs) {
    return true;
  }

  // /positions tiene prioridad sobre /above solamente mientras esta activa.
  if (isPositionsRequestInProgress()) {
    return true;
  }

  // Durante el backoff no crear tareas inútiles.
  if (aboveBackoffActive()) {
    return true;
  }

  // Un clic repetido en "Actualizar visibles" tampoco debe consumir cuota.
  if (force &&
      gLastAboveAttemptMs != 0 &&
      (millis() - gLastAboveAttemptMs) <
          kManualMinRequestMs) {
    Serial.println(
        "N2YO /above: actualización manual ignorada "
        "(mínimo 60 s)"
    );
    return true;
  }

  if (isAnyMotionBusy()) {
    return true;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  portENTER_CRITICAL(&gRefreshMux);

  if (gRefreshInProgress) {
    portEXIT_CRITICAL(&gRefreshMux);
    return true;
  }

  gRefreshInProgress = true;

  portEXIT_CRITICAL(&gRefreshMux);

  BaseType_t ok = xTaskCreate(
      visibleRefreshTask,
      "visible_now",
      8192,
      nullptr,
      1,
      &gRefreshTaskHandle
  );

  if (ok != pdPASS) {
    portENTER_CRITICAL(&gRefreshMux);
    gRefreshInProgress = false;
    gRefreshTaskHandle = nullptr;
    portEXIT_CRITICAL(&gRefreshMux);

    Serial.println("No se pudo crear tarea visible_now");
    return false;
  }

  return true;
}


bool startVisibleNowAutoRefresh() {
  ensureVisibleMutex();
  gLastAutoServiceMs = millis() - kAutoServiceTickMs;
  Serial.println(
      "Visibles: servicio sin tarea; cache /above cada 1 min"
  );
  return true;
}

void serviceVisibleNowAutoRefresh() {
  const uint32_t now = millis();
  if ((now - gLastAutoServiceMs) < kAutoServiceTickMs) return;
  gLastAutoServiceMs = now;

  if (isAnyMotionBusy()) return;

  // Intentar primero una unica consulta /above. Si no puede resolver todos
  // los favoritos, el respaldo avanza uno a uno cada 15 s.
  requestVisibleNowRefreshIfStale(kAutoRefreshAgeMs);
  if (!isVisibleNowRefreshInProgress()) {
    requestFavoriteVisibilityRefreshIfStale();
  }
}

SatVisibleState getVisibleNowState(long noradId) {
  if (noradId <= 0) {
    return SAT_VIS_UNKNOWN;
  }

  // El satélite seleccionado se conoce inmediatamente por su propia
  // predicción /positions, sin esperar a ninguna caché de visibles.
  if (noradId == satid &&
      selectedPredictionValid()) {
    return (satelevat >= kMinElevationDeg)
        ? SAT_VIS_VISIBLE
        : SAT_VIS_NOT_VISIBLE;
  }

  // La instantanea global /above es la fuente preferente: una respuesta
  // completa clasifica todos los favoritos con una sola conexion.
  SatVisibleState result = SAT_VIS_UNKNOWN;

  lockVisible();

  if (!gHasSnapshot) {
    result = SAT_VIS_UNKNOWN;
  } else {
    bool found = false;

    for (int i = 0; i < gVisibleIdCount; ++i) {
      if (gVisibleIds[i] == noradId) {
        found = true;
        break;
      }
    }

    if (found) {
      result = SAT_VIS_VISIBLE;
    } else if (!gVisibleIdOverflow) {
      result = SAT_VIS_NOT_VISIBLE;
    }
  }

  unlockVisible();
  if (result != SAT_VIS_UNKNOWN) return result;

  // Si /above fallo o su lista excedio la cache, usar el resultado individual
  // de /positions para ese favorito.
  return getFreshFavoriteVisState(noradId);
}

bool isVisibleNow(long noradId) {
  return getVisibleNowState(noradId) == SAT_VIS_VISIBLE;
}
