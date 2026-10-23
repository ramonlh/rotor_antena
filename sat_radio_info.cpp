

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <FFat.h>
#include "sat_radio_info.h"
#include "sat_fetch.h"
#include "http_buffer.h"
#include "fixed_text.h"
#include "storage_services.h"

struct SatTxInfo {
  FixedText<64> description;
  FixedText<24> downlink;
  FixedText<24> uplink;
  FixedText<24> mode;
  FixedText<20> service;
  FixedText<16> status;
};

static SatTxInfo gTx[12];
static int gTxCount = 0;
static long gTxNorad = 0;
static bool gTxLoaded = false;

static FixedText<48> gRadioSource = "none";
static int gLastRadioHttpCode = 0;

static constexpr uint32_t kRadioCacheMagic = 0x52414431UL;
static constexpr uint16_t kRadioCacheVersion = 1;

struct RadioCache {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  int32_t norad;
  SatTxInfo tx[12];
  uint32_t check;
};
static RadioCache gRadioCacheBuffer;

static void radioCachePaths(
    long noradId,
    char* cachePath,
    size_t cachePathSize,
    char* tempPath,
    size_t tempPathSize) {
  snprintf(cachePath, cachePathSize, "/radio_%ld.bin", noradId);
  snprintf(tempPath, tempPathSize, "/radio_%ld.tmp", noradId);
}

static uint32_t radioCacheCheck(const RadioCache& cache) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&cache);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < sizeof(cache) - sizeof(cache.check); ++i) {
    hash = (hash ^ bytes[i]) * 16777619UL;
  }
  return hash;
}

static bool loadRadioCache(long noradId) {
  if (!isStorageReady()) return false;
  char cachePath[32];
  char tempPath[32];
  radioCachePaths(
      noradId, cachePath, sizeof(cachePath), tempPath, sizeof(tempPath));
  File file = FFat.open(cachePath, "r");
  if (!file) return false;
  RadioCache& cache = gRadioCacheBuffer;
  memset(&cache, 0, sizeof(cache));
  const size_t read = file.read(
      reinterpret_cast<uint8_t*>(&cache), sizeof(cache));
  file.close();
  if (read != sizeof(cache) ||
      cache.magic != kRadioCacheMagic ||
      cache.version != kRadioCacheVersion ||
      cache.count > 12 || cache.count == 0 ||
      cache.norad != noradId ||
      cache.check != radioCacheCheck(cache)) return false;

  gTxCount = cache.count;
  gTxNorad = cache.norad;
  gTxLoaded = true;
  for (int i = 0; i < gTxCount; ++i) gTx[i] = cache.tx[i];
  return true;
}

static bool saveRadioCache() {
  if (!isStorageReady() || !gTxLoaded || gTxCount <= 0) return false;
  RadioCache& cache = gRadioCacheBuffer;
  memset(&cache, 0, sizeof(cache));
  cache.magic = kRadioCacheMagic;
  cache.version = kRadioCacheVersion;
  cache.count = gTxCount;
  cache.norad = gTxNorad;
  for (int i = 0; i < gTxCount; ++i) cache.tx[i] = gTx[i];
  cache.check = radioCacheCheck(cache);

  char cachePath[32];
  char tempPath[32];
  radioCachePaths(
      gTxNorad, cachePath, sizeof(cachePath), tempPath, sizeof(tempPath));
  FFat.remove(tempPath);
  File file = FFat.open(tempPath, "w");
  if (!file) return false;
  const size_t written = file.write(
      reinterpret_cast<const uint8_t*>(&cache), sizeof(cache));
  file.close();
  if (written != sizeof(cache)) {
    FFat.remove(tempPath);
    return false;
  }
  FFat.remove(cachePath);
  if (!FFat.rename(tempPath, cachePath)) {
    FFat.remove(tempPath);
    return false;
  }
  return true;
}

static const char* radioObjectEnd(const char* object) {
  int depth = 0;
  bool quoted = false;
  bool escaped = false;
  for (const char* p = object; *p != '\0'; ++p) {
    if (quoted) {
      if (escaped) escaped = false;
      else if (*p == '\\') escaped = true;
      else if (*p == '\"') quoted = false;
      continue;
    }
    if (*p == '\"') quoted = true;
    else if (*p == '{') ++depth;
    else if (*p == '}' && --depth == 0) return p;
  }
  return nullptr;
}

template <size_t N>
static bool radioJsonString(
    const char* object, const char* objectEnd,
    const char* key, FixedText<N>& value) {
  const char* field = strstr(object, key);
  if (field == nullptr || field >= objectEnd) return false;
  const char* colon = strchr(field, ':');
  if (colon == nullptr || colon >= objectEnd) return false;
  while (++colon < objectEnd && (*colon == ' ' || *colon == '\t')) {}
  if (colon >= objectEnd || *colon != '\"') return false;
  char decoded[N];
  size_t length = 0;
  for (const char* p = colon + 1; p < objectEnd; ++p) {
    if (*p == '\"') {
      decoded[length] = '\0';
      value = decoded;
      return true;
    }
    if (*p == '\\' && p + 1 < objectEnd) ++p;
    if (length + 1 < N) decoded[length++] = *p;
  }
  return false;
}

static bool radioJsonNumber(
    const char* object, const char* objectEnd,
    const char* key, double& value) {
  const char* field = strstr(object, key);
  if (field == nullptr || field >= objectEnd) return false;
  const char* colon = strchr(field, ':');
  char* end = nullptr;
  value = colon != nullptr ? strtod(colon + 1, &end) : 0.0;
  return colon != nullptr && end != colon + 1 && end <= objectEnd;
}

static void clearTxCache() {
  gTxCount = 0;
  gTxNorad = 0;
  gTxLoaded = false;
  gRadioSource = "none";
  gLastRadioHttpCode = 0;     
  for (int i = 0; i < 12; ++i) {
    gTx[i].description = "";
    gTx[i].downlink = "";
    gTx[i].uplink = "";
    gTx[i].mode = "";
    gTx[i].service = "";
    gTx[i].status = "";

  }
}

static void setHzText(double hz, FixedText<24>& target) {
  if (hz <= 0.0) {
    target = "-";
    return;
  }
  char text[24];
  snprintf(text, sizeof(text), "%.6f MHz", hz / 1000000.0);
  target = text;
}

bool refreshRadioInfoForSat(long noradId) {
  clearTxCache();

  const bool haveCache = loadRadioCache(noradId);

  Serial.println("=== refreshRadioInfoForSat ===");
  Serial.print("NORAD solicitado: ");
  Serial.println(noradId);
  Serial.print("Cache FFat disponible: ");
  Serial.println(haveCache ? "SI" : "NO");

  if (WiFi.status() != WL_CONNECTED) {
    gRadioSource = haveCache ? "caché FFat (sin WiFi)" : "sin datos (sin WiFi)";
    return haveCache;
  }

  // No abrir dos conexiones TLS a la vez: en el ESP32 los buffers de
  // N2YO y SatNOGS juntos pueden agotar o fragmentar el heap.
  if (!tryBeginN2yoRequest()) {
    gRadioSource = haveCache
        ? "caché FFat (red ocupada)"
        : "sin datos (red ocupada)";
    return haveCache;
  }

  HTTPClient http;

  char url[160];
  snprintf(url, sizeof(url),
           "https://db.satnogs.org/api/transmitters/?format=json&alive=true&satellite__norad_cat_id=%ld",
           noradId);
  Serial.print("URL SatNOGS: ");
  Serial.println(url);

  http.begin(url);
  http.useHTTP10(true);
  http.setConnectTimeout(3000);
  http.setTimeout(12000);
  int code = http.GET();
  gLastRadioHttpCode = code;
  Serial.print("HTTP code: ");
  Serial.println(code);

  if (code != HTTP_CODE_OK) {
    http.end();
    endN2yoRequest();
    gRadioSource = haveCache ? "caché FFat (HTTP fallo)" : "sin datos (HTTP fallo)";
    return haveCache;
  }

  const char* payload = nullptr;
  size_t payloadLength = 0;
  // Sólo se muestran 12 transmisores: si la respuesta completa supera el
  // búfer, son suficientes los primeros objetos completos recibidos.
  const bool bodyOk = readHttpBody(http, payload, payloadLength, true);
  http.end();
  if (!bodyOk) {
    endN2yoRequest();
    gRadioSource = haveCache
        ? "caché FFat (respuesta incompleta)"
        : "sin datos (respuesta incompleta)";
    return haveCache;
  }

  Serial.print("Payload len: ");
  Serial.println(payloadLength);

  const char* json = payload;
  while (*json == ' ' || *json == '\r' || *json == '\n' || *json == '\t') ++json;
  if (*json != '[') {
    gRadioSource = haveCache ? "caché FFat (JSON inválido)" : "sin datos (JSON inválido)";
    endN2yoRequest();
    return haveCache;
  }

  clearTxCache();
  gTxNorad = noradId;

  int n = 0;
  const char* cursor = json + 1;
  while (*cursor != '\0' && gTxCount < 12) {
    const char* object = strchr(cursor, '{');
    if (object == nullptr) break;
    const char* objectEnd = radioObjectEnd(object);
    if (objectEnd == nullptr) break;
    cursor = objectEnd + 1;
    ++n;

    radioJsonString(object, objectEnd, "\"description\"", gTx[gTxCount].description);
    radioJsonString(object, objectEnd, "\"mode\"", gTx[gTxCount].mode);
    radioJsonString(object, objectEnd, "\"service\"", gTx[gTxCount].service);
    radioJsonString(object, objectEnd, "\"status\"", gTx[gTxCount].status);

    double frequency = 0.0;
    if (radioJsonNumber(object, objectEnd, "\"downlink_low\"", frequency) ||
        radioJsonNumber(object, objectEnd, "\"downlink\"", frequency))
      setHzText(frequency, gTx[gTxCount].downlink);
    else gTx[gTxCount].downlink = "-";

    if (radioJsonNumber(object, objectEnd, "\"uplink_low\"", frequency) ||
        radioJsonNumber(object, objectEnd, "\"uplink\"", frequency))
      setHzText(frequency, gTx[gTxCount].uplink);
    else gTx[gTxCount].uplink = "-";

    Serial.print("TX ");
    Serial.print(gTxCount);
    Serial.print(": ");
    Serial.print(gTx[gTxCount].description.c_str());
    Serial.print(" | DL=");
    Serial.print(gTx[gTxCount].downlink);
    Serial.print(" | UL=");
    Serial.print(gTx[gTxCount].uplink);
    Serial.print(" | MODE=");
    Serial.println(gTx[gTxCount].mode.c_str());

    ++gTxCount;
  }

  Serial.print("Transmitters analizados: ");
  Serial.println(n);

  gTxLoaded = (gTxCount > 0);
  endN2yoRequest();

  Serial.print("TX matches finales: ");
  Serial.println(gTxCount);

  if (!gTxLoaded) {
    const bool restored = loadRadioCache(noradId);
    gRadioSource = restored ? "caché FFat (sin TX online)" : "sin datos";
    return restored;
  }

  saveRadioCache();
  gRadioSource = "SatNOGS";
  return true;
}

String buildRadioInfoHtml(long noradId) {
  if (!gTxLoaded || gTxNorad != noradId) {
    refreshRadioInfoForSat(noradId);
  }

  String out;
  out.reserve(4096);
  out += "<div class='radioinfo'>";

  if (!gTxLoaded || gTxCount == 0) {
    out += "<div class='radio-empty'>Sin datos de radio para este satélite.</div></div>";
    return out;
  }

  out += "<div class='radio-table'>";
  out += "<div class='radio-row radio-labels'><span>Transmisor</span>";
  out += "<span>Bajada (RX)</span><span>Subida (TX)</span>";
  out += "<span>Modo · servicio · estado</span></div>";
  for (int i = 0; i < gTxCount; ++i) {
    out += "<div class='radio-row'><span class='radio-desc'>";
    if (gTx[i].description.length()) out += gTx[i].description.c_str();
    else out += "Transmisor";
    out += "</span><span class='radio-freq'>";
    out += gTx[i].downlink.c_str();
    out += "</span><span class='radio-freq'>";
    out += gTx[i].uplink.c_str();
    out += "</span><span class='radio-meta'>";
    if (gTx[i].mode.length()) out += gTx[i].mode.c_str();
    if (gTx[i].service.length()) {
      if (gTx[i].mode.length()) out += " · ";
      out += gTx[i].service.c_str();
    }
    if (gTx[i].status.length()) {
      out += " · ";
      out += gTx[i].status.c_str();
    }
    out += "</span></div>";
  }

  out += "</div></div>";

  return out;
}

const char* getRadioInfoSourceText() {
  return gRadioSource.c_str();
}

int getLastRadioHttpCode() {
  return gLastRadioHttpCode;
}

long getRadioInfoNoradId() {
  return gTxNorad;
}
