#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "sat_fetch.h"
#include "app_state.h"
#include "config.h"
#include "diagnostic_log.h"
#include "http_buffer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"

static portMUX_TYPE gN2yoMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool gN2yoBusy = false;
static portMUX_TYPE gPositionsMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool gPositionsTaskRunning = false;
static TaskHandle_t gPositionsTaskHandle = nullptr;
static long gRequestedSatId = 0;
static unsigned long gPositionsRetryAfterMs = 0;
static unsigned long gLastPredictionOkMs = 0;
static char gLastPredictionError[256] = "Sin consultas";
static bool gResultPending = false;
static long gResultSatId = 0;
static uint32_t gResultTime[kTableSize] = {0};
static float gResultAz[kTableSize] = {0.0f};
static float gResultEl[kTableSize] = {0.0f};
static char gResultName[64] = "";
static constexpr unsigned long kPositionsErrorRetryMs = 30000UL;
static constexpr unsigned long kPositionsTransportRetryBaseMs = 30000UL;
static constexpr unsigned long kPositionsNoDataRetryMs = 10UL * 60UL * 1000UL;
static uint8_t gConsecutiveTransportErrors = 0;

// N2YO devuelve muchos campos por posición. Construir todo el árbol con
// cJSON consume decenas de KiB justo cuando TLS mantiene sus buffers. Este
// lector extrae solamente los cuatro valores que usa el rotor.
static bool parsePositionsPayload(
    const char* json,
    uint32_t* times,
    float* azimuths,
    float* elevations,
    char* satName,
    size_t satNameSize,
    char* error,
    size_t errorSize) {

  while (*json == ' ' || *json == '\r' || *json == '\n' || *json == '\t') ++json;
  if (*json != '{') {
    strlcpy(error, "respuesta no JSON", errorSize);
    return false;
  }

  const char* apiError = strstr(json, "\"error\"");
  const char* positions = strstr(json, "\"positions\"");
  if (apiError != nullptr && (positions == nullptr || apiError < positions)) {
    const char* colon = strchr(apiError, ':');
    const char* firstQuote = colon != nullptr ? strchr(colon, '\"') : nullptr;
    const char* lastQuote = firstQuote != nullptr ? strchr(firstQuote + 1, '\"') : nullptr;
    if (firstQuote != nullptr && lastQuote != nullptr) {
      const size_t n = min((size_t)(lastQuote - firstQuote - 1), errorSize - 1);
      memcpy(error, firstQuote + 1, n);
      error[n] = '\0';
    } else {
      strlcpy(error, "error de API", errorSize);
    }
    return false;
  }

  if (positions == nullptr) {
    strlcpy(error, "sin array positions", errorSize);
    return false;
  }

  const char* arrayStart = strchr(positions, '[');
  const char* arrayEnd = arrayStart != nullptr ? strchr(arrayStart, ']') : nullptr;
  if (arrayStart == nullptr || arrayEnd == nullptr) {
    strlcpy(error, "array positions incompleto", errorSize);
    return false;
  }

  int count = 0;
  const char* cursor = arrayStart + 1;
  while (cursor < arrayEnd && count < kTableSize) {
    const char* azKey = strstr(cursor, "\"azimuth\"");
    if (azKey == nullptr || azKey >= arrayEnd) break;
    const char* objectEnd = strchr(azKey, '}');
    const char* elKey = strstr(azKey, "\"elevation\"");
    const char* timeKey = elKey != nullptr ? strstr(elKey, "\"timestamp\"") : nullptr;
    if (objectEnd == nullptr || objectEnd > arrayEnd ||
        elKey == nullptr || elKey > objectEnd ||
        timeKey == nullptr || timeKey > objectEnd) {
      strlcpy(error, "posicion incompleta", errorSize);
      return false;
    }

    const char* azColon = strchr(azKey, ':');
    const char* elColon = strchr(elKey, ':');
    const char* timeColon = strchr(timeKey, ':');
    char* numberEnd = nullptr;
    azimuths[count] = strtof(azColon + 1, &numberEnd);
    if (numberEnd == azColon + 1) {
      strlcpy(error, "azimuth invalido", errorSize);
      return false;
    }
    elevations[count] = strtof(elColon + 1, &numberEnd);
    if (numberEnd == elColon + 1) {
      strlcpy(error, "elevation invalida", errorSize);
      return false;
    }
    times[count] = strtoul(timeColon + 1, &numberEnd, 10);
    if (numberEnd == timeColon + 1 || times[count] == 0) {
      strlcpy(error, "timestamp invalido", errorSize);
      return false;
    }
    ++count;
    cursor = objectEnd + 1;
  }

  if (count <= 0) {
    strlcpy(error, "sin posiciones", errorSize);
    return false;
  }

  const char* nameKey = strstr(json, "\"satname\"");
  if (nameKey != nullptr && nameKey < positions) {
    const char* colon = strchr(nameKey, ':');
    const char* firstQuote = colon != nullptr ? strchr(colon, '\"') : nullptr;
    const char* lastQuote = firstQuote != nullptr ? strchr(firstQuote + 1, '\"') : nullptr;
    if (firstQuote != nullptr && lastQuote != nullptr) {
      const size_t n = min((size_t)(lastQuote - firstQuote - 1), satNameSize - 1);
      memcpy(satName, firstQuote + 1, n);
      satName[n] = '\0';
    }
  }
  return true;
}

static bool positionsRetryBlocked() {
  portENTER_CRITICAL(&gPositionsMux);
  const unsigned long retryAt = gPositionsRetryAfterMs;
  portEXIT_CRITICAL(&gPositionsMux);
  return retryAt != 0 && (int32_t)(retryAt - millis()) > 0;
}

static void setPredictionError(
    const char* text,
    int response,
    unsigned long retryMs = kPositionsErrorRetryMs) {
  portENTER_CRITICAL(&gPositionsMux);
  strlcpy(gLastPredictionError, text, sizeof(gLastPredictionError));
  lastresponse = response;
  gPositionsRetryAfterMs = millis() + retryMs;
  portEXIT_CRITICAL(&gPositionsMux);
  addDiagnosticEvent(text);
  Serial.println(text);
}

bool tryBeginN2yoRequest() {
  bool ok = false;
  portENTER_CRITICAL(&gN2yoMux);
  if (!gN2yoBusy) {
    gN2yoBusy = true;
    ok = true;
  }
  portEXIT_CRITICAL(&gN2yoMux);
  return ok;
}

void endN2yoRequest() {
  portENTER_CRITICAL(&gN2yoMux);
  gN2yoBusy = false;
  portEXIT_CRITICAL(&gN2yoMux);
}

bool isN2yoRequestBusy() {
  portENTER_CRITICAL(&gN2yoMux);
  const bool busy = gN2yoBusy;
  portEXIT_CRITICAL(&gN2yoMux);
  return busy;
}

static void finishPositionsTask() {
  portENTER_CRITICAL(&gPositionsMux);
  gPositionsTaskRunning = false;
  gPositionsTaskHandle = nullptr;
  portEXIT_CRITICAL(&gPositionsMux);
}

// Esta función retorna normalmente para ejecutar los destructores de
// HTTPClient y String antes de eliminar la tarea FreeRTOS.
static void positionsWorker() {
  long requestedId;
  portENTER_CRITICAL(&gPositionsMux);
  requestedId = gRequestedSatId;
  portEXIT_CRITICAL(&gPositionsMux);

  if (WiFi.status() != WL_CONNECTED) {
    setPredictionError("N2YO: WiFi no conectada", -1);
    return;
  }
  if (!tryBeginN2yoRequest()) {
    return;
  }

  char serverPath[256];
  snprintf(serverPath, sizeof(serverPath), "%s%ld/%.6f/%.6f/%d/%lu/&apiKey=%s",
           SERVER_POS_BASE, requestedId, MY_LAT, MY_LON, MY_ALT_M,
           (unsigned long)kPredictionWindowSec, N2YO_API_KEY);
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  secureClient.setHandshakeTimeout(10);
  HTTPClient http;
  http.begin(secureClient, serverPath);
  http.useHTTP10(true);
  http.setConnectTimeout(5000);
  http.setTimeout(20000);
  const int response = http.GET();
  if (response != HTTP_CODE_OK) {
    char tlsText[96] = "";
    const int tlsCode = secureClient.lastError(tlsText, sizeof(tlsText));
    http.end();
    endN2yoRequest();
    String message = "N2YO /positions ";
    message += String(response);
    message += ": ";
    message += HTTPClient::errorToString(response);
    if (tlsCode < 0) {
      message += "; TLS ";
      message += String(tlsCode);
      message += " ";
      message += tlsText;
    }
    IPAddress resolved;
    if (WiFi.hostByName("api.n2yo.com", resolved)) {
      message += "; DNS ";
      message += resolved.toString();
      WiFiClient probe;
      probe.setTimeout(2000);
      if (probe.connect(resolved, 443)) {
        message += "; TCP 443 OK";
        probe.stop();
      } else {
        message += "; TCP 443 rechazado";
      }
    } else {
      message += "; DNS fallido";
    }
    unsigned long retryMs = kPositionsErrorRetryMs;
    if (response < 0) {
      if (gConsecutiveTransportErrors < 4) ++gConsecutiveTransportErrors;
      retryMs = kPositionsTransportRetryBaseMs <<
          (gConsecutiveTransportErrors - 1);
    }
    setPredictionError(message.c_str(), response, retryMs);
    return;
  }
  gConsecutiveTransportErrors = 0;

  const char* payload = nullptr;
  size_t payloadLength = 0;
  const bool bodyOk = readHttpBody(http, payload, payloadLength);
  http.end();
  if (!bodyOk) {
    endN2yoRequest();
    String message = "N2YO /positions: cuerpo ";
    message += String(payloadLength);
    message += "/";
    message += String(getHttpBufferCapacity());
    message += " bytes (vacio o demasiado grande)";
    setPredictionError(message.c_str(), -3);
    return;
  }

  uint32_t nextTime[kTableSize] = {0};
  float nextAz[kTableSize] = {0.0f};
  float nextEl[kTableSize] = {0.0f};
  char nextName[64] = "";
  char parseError[72] = "";
  if (!parsePositionsPayload(
          payload, nextTime, nextAz, nextEl,
          nextName, sizeof(nextName),
          parseError, sizeof(parseError))) {
    endN2yoRequest();
    String message = "N2YO /positions: " + String(parseError);
    const bool noData = strcmp(parseError, "sin array positions") == 0 ||
                        strcmp(parseError, "sin posiciones") == 0;
    setPredictionError(
        message.c_str(),
        noData ? -2 : -3,
        noData ? kPositionsNoDataRetryMs : kPositionsErrorRetryMs);
    return;
  }

  // Entregar al bucle principal mediante un buzón. La tarea de red nunca
  // modifica String ni el estado activo que consume el control del rotor.
  portENTER_CRITICAL(&gPositionsMux);
  gResultSatId = requestedId;
  for (int i = 0; i < kTableSize; ++i) {
    gResultTime[i] = nextTime[i];
    gResultAz[i] = nextAz[i];
    gResultEl[i] = nextEl[i];
  }
  strlcpy(gResultName, nextName, sizeof(gResultName));
  gResultPending = true;
  portEXIT_CRITICAL(&gPositionsMux);

  endN2yoRequest();
}

static void positionsTask(void* parameter) {
  (void)parameter;
  positionsWorker();
  finishPositionsTask();
  vTaskDelete(nullptr);
}

bool initPositionsService() {
  // La tarea se crea bajo demanda y se elimina al terminar para devolver su
  // pila al heap antes de que otro servicio necesite negociar TLS.
  return true;
}

void getPositions() {
  timegetpositions = millis();
  if (satid <= 0 || positionsRetryBlocked()) return;

  portENTER_CRITICAL(&gPositionsMux);
  if (gPositionsTaskRunning) {
    portEXIT_CRITICAL(&gPositionsMux);
    return;
  }
  gRequestedSatId = satid;
  gPositionsTaskRunning = true;
  portEXIT_CRITICAL(&gPositionsMux);

  const BaseType_t ok = xTaskCreatePinnedToCore(
      positionsTask, "n2yo_positions", 6144, nullptr, 1,
      &gPositionsTaskHandle, 0);
  if (ok != pdPASS) {
    finishPositionsTask();
    setPredictionError("No se pudo iniciar tarea N2YO", -4);
  }
}

void servicePositions() {
  uint32_t nextTime[kTableSize];
  float nextAz[kTableSize];
  float nextEl[kTableSize];
  char nextName[sizeof(gResultName)];

  portENTER_CRITICAL(&gPositionsMux);
  if (!gResultPending) {
    portEXIT_CRITICAL(&gPositionsMux);
    return;
  }

  // Una respuesta de una selección anterior se descarta.
  if (gResultSatId != satid) {
    gResultPending = false;
    portEXIT_CRITICAL(&gPositionsMux);
    return;
  }

  for (int i = 0; i < kTableSize; ++i) {
    nextTime[i] = gResultTime[i];
    nextAz[i] = gResultAz[i];
    nextEl[i] = gResultEl[i];
  }
  strlcpy(nextName, gResultName, sizeof(nextName));
  gResultPending = false;
  portEXIT_CRITICAL(&gPositionsMux);

  tabunixtime[0] = 0;
  for (int i = 1; i < kTableSize; ++i) {
    tabunixtime[i] = nextTime[i];
    tabazimut[i] = nextAz[i];
    tabelevat[i] = nextEl[i];
  }
  tabazimut[0] = nextAz[0];
  tabelevat[0] = nextEl[0];
  satazimut = nextAz[0];
  satelevat = nextEl[0];
  unixtime = nextTime[0] - 1;
  if (nextName[0] != '\0') satname = String(nextName);
  tabunixtime[0] = nextTime[0];

  portENTER_CRITICAL(&gPositionsMux);
  lastresponse = HTTP_CODE_OK;
  gPositionsRetryAfterMs = 0;
  gConsecutiveTransportErrors = 0;
  gLastPredictionOkMs = millis();
  strlcpy(gLastPredictionError, "Sin errores", sizeof(gLastPredictionError));
  portEXIT_CRITICAL(&gPositionsMux);
}

bool isPositionsRequestInProgress() {
  portENTER_CRITICAL(&gPositionsMux);
  const bool running = gPositionsTaskRunning;
  portEXIT_CRITICAL(&gPositionsMux);
  return running;
}

uint32_t getLastPredictionAgeMs() {
  portENTER_CRITICAL(&gPositionsMux);
  const unsigned long value = gLastPredictionOkMs;
  portEXIT_CRITICAL(&gPositionsMux);
  return value == 0 ? UINT32_MAX : millis() - value;
}

uint32_t getPositionsRetryRemainingMs() {
  portENTER_CRITICAL(&gPositionsMux);
  const unsigned long retryAt = gPositionsRetryAfterMs;
  portEXIT_CRITICAL(&gPositionsMux);
  if (retryAt == 0 || (int32_t)(retryAt - millis()) <= 0) return 0;
  return retryAt - millis();
}

String getLastPredictionError() {
  char copy[sizeof(gLastPredictionError)];
  portENTER_CRITICAL(&gPositionsMux);
  strlcpy(copy, gLastPredictionError, sizeof(copy));
  portEXIT_CRITICAL(&gPositionsMux);
  return String(copy);
}

void resetPositionsStatus() {
  portENTER_CRITICAL(&gPositionsMux);
  gLastPredictionOkMs = 0;
  gPositionsRetryAfterMs = 0;
  strlcpy(gLastPredictionError, "Esperando prediccion", sizeof(gLastPredictionError));
  gResultPending = false;
  portEXIT_CRITICAL(&gPositionsMux);
}
