#include <Arduino.h>
#include <math.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <FFat.h>
#include "sat_search.h"
#include "storage_services.h"

static constexpr int kSearchMax = 20;
static constexpr int kAmateurCatalogMax = 110;

static const char* kAmateurCacheFile = "/amateur_catalog.tsv";
static const char* kAmateurTempFile  = "/amateur_catalog.tmp";

// CelesTrak indica que los datos GP se actualizan aproximadamente cada 2 h.
// No tiene sentido volver a pedir el mismo GROUP antes.
static constexpr unsigned long kCelestrakMinRefreshMs =
    2UL * 60UL * 60UL * 1000UL;

static bool gRemoteRefreshAttempted = false;
static unsigned long gLastRemoteRefreshMs = 0;

static SatSearchItem gSearchResults[kSearchMax];
static int gSearchCount = 0;

static AmateurCatalogItem gAmateurCatalog[kAmateurCatalogMax];
static int gAmateurCatalogCount = 0;
static String gAmateurCatalogStatus = "No cargado";

static SatRadioInspection gInspection = {
  false, false, 0, "",
  0, 0, 0, 0, 0,
  "", "", "", "", "", ""
};

static const char* searchObjectEnd(const char* object) {
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

static bool readObjectLong(
    const char* object, const char* objectEnd,
    const char* key, long& value) {
  const char* field = strstr(object, key);
  if (field == nullptr || field >= objectEnd) return false;
  const char* colon = strchr(field, ':');
  char* end = nullptr;
  value = colon != nullptr ? strtol(colon + 1, &end, 10) : 0;
  return colon != nullptr && end != colon + 1 && end <= objectEnd;
}

static bool readObjectString(
    const char* object, const char* objectEnd,
    const char* key, String& value) {
  const char* field = strstr(object, key);
  if (field == nullptr || field >= objectEnd) return false;
  const char* colon = strchr(field, ':');
  const char* quote = colon != nullptr ? strchr(colon, '\"') : nullptr;
  if (quote == nullptr || quote >= objectEnd) return false;
  value = "";
  for (const char* p = quote + 1; p < objectEnd; ++p) {
    if (*p == '\"') return true;
    if (*p == '\\' && p + 1 < objectEnd) ++p;
    value += *p;
  }
  value = "";
  return false;
}

static bool readObjectDouble(
    const char* object, const char* objectEnd,
    const char* key, double& value) {
  const char* field = strstr(object, key);
  if (field == nullptr || field >= objectEnd) return false;
  const char* colon = strchr(field, ':');
  char* end = nullptr;
  value = colon != nullptr ? strtod(colon + 1, &end) : 0.0;
  return colon != nullptr && end != colon + 1 && end <= objectEnd;
}

static String urlEncode(const String& s) {
  String out;
  out.reserve(s.length() + 8);

  const char* hex = "0123456789ABCDEF";

  for (size_t i = 0; i < s.length(); ++i) {
    const uint8_t c = (uint8_t)s[i];

    if ((c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }

  return out;
}

static String csvField(const String& line, int wanted) {
  String out;
  int field = 0;
  bool quoted = false;

  for (int i = 0; i <= (int)line.length(); ++i) {
    const char c =
        (i < (int)line.length()) ? line[i] : ',';

    if (quoted) {
      if (c == '"') {
        if (i + 1 < (int)line.length() &&
            line[i + 1] == '"') {
          out += '"';
          ++i;
        } else {
          quoted = false;
        }
      } else {
        out += c;
      }
      continue;
    }

    if (c == '"') {
      quoted = true;
      continue;
    }

    if (c == ',') {
      if (field == wanted) {
        out.trim();
        return out;
      }

      ++field;
      out = "";
      continue;
    }

    out += c;
  }

  return "";
}

static int csvColumnIndex(
    const String& header,
    const char* wanted) {

  for (int i = 0; i < 64; ++i) {
    String f = csvField(header, i);

    if (f.length() == 0 && i > 20) {
      break;
    }

    f.trim();

    if (f == wanted) {
      return i;
    }
  }

  return -1;
}

template <size_t N>
static void appendUnique(
    FixedText<N>& dst,
    const String& value,
    size_t maxLen = 180) {

  String v = value;
  v.trim();

  const size_t limit = min(maxLen, N - 1);
  if (v.length() == 0 || v == "-" || dst.length() >= limit) {
    return;
  }

  // Buscar por tokens sin construir dos String auxiliares.
  const char* token = dst.c_str();
  while (*token != '\0') {
    const char* end = strstr(token, ", ");
    const size_t tokenLength = end != nullptr
        ? size_t(end - token) : strlen(token);
    if (tokenLength == v.length() &&
        strncmp(token, v.c_str(), tokenLength) == 0) return;
    if (end == nullptr) break;
    token = end + 2;
  }

  if (dst.length()) dst.append(", ");
  dst.append(v.c_str());
}

static String hzRangeToMHz(
    const char* object,
    const char* objectEnd,
    const char* lowKey,
    const char* highKey) {

  double low = 0.0;
  double high = 0.0;

  readObjectDouble(object, objectEnd, lowKey, low);
  readObjectDouble(object, objectEnd, highKey, high);

  if (low <= 0.0 && high <= 0.0) {
    return "";
  }

  if (low <= 0.0) low = high;
  if (high <= 0.0) high = low;

  if (fabs(high - low) < 1.0) {
    return String(low / 1000000.0, 4) + " MHz";
  }

  return String(low / 1000000.0, 4) +
         "-" +
         String(high / 1000000.0, 4) +
         " MHz";
}

static bool hasPositiveJsonNumber(
    const char* object,
    const char* objectEnd,
    const char* key) {
  double value = 0.0;
  return readObjectDouble(object, objectEnd, key, value) && value > 0.0;
}

void clearSatSearchResults() {
  gSearchCount = 0;

  for (int i = 0; i < kSearchMax; ++i) {
    gSearchResults[i].noradId = 0;
    gSearchResults[i].name = "";
  }
}

int getSatSearchResultCount() {
  return gSearchCount;
}

SatSearchItem getSatSearchResult(int index) {
  SatSearchItem empty{0, ""};

  if (index < 0 || index >= gSearchCount) {
    return empty;
  }

  return gSearchResults[index];
}

bool searchSatellitesByName(const String& query) {
  clearSatSearchResults();
  clearSatRadioInspection();

  String q = query;
  q.trim();

  if (q.length() < 2) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  String url =
      "https://celestrak.org/satcat/records.php?NAME=" +
      urlEncode(q) +
      "&PAYLOADS=1&ACTIVE=1&ONORBIT=1&MAX=20&FORMAT=JSON";

  Serial.println("=== searchSatellitesByName ===");
  Serial.print("URL: ");
  Serial.println(url);

  HTTPClient http;
  http.begin(url);
  http.setConnectTimeout(3000);
  http.setTimeout(7000);

  const int code = http.GET();

  Serial.print("HTTP code: ");
  Serial.println(code);

  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();

  const char* json = payload.c_str();
  while (*json == ' ' || *json == '\r' || *json == '\n' || *json == '\t') ++json;
  if (*json != '[') {
    Serial.println("JSON no es array");
    return false;
  }

  const char* cursor = json + 1;
  while (*cursor != '\0' && gSearchCount < kSearchMax) {
    const char* object = strchr(cursor, '{');
    if (object == nullptr) break;
    const char* objectEnd = searchObjectEnd(object);
    if (objectEnd == nullptr) break;
    long id = 0;
    String name;
    if (readObjectLong(object, objectEnd, "\"NORAD_CAT_ID\"", id) &&
        readObjectString(object, objectEnd, "\"OBJECT_NAME\"", name) &&
        id > 0) {
      gSearchResults[gSearchCount].noradId = id;
      gSearchResults[gSearchCount].name = name;
      ++gSearchCount;
    }
    cursor = objectEnd + 1;
  }

  Serial.print("Resultados guardados: ");
  Serial.println(gSearchCount);

  return gSearchCount > 0;
}

static bool loadAmateurCatalogFromFile(
    const char* path,
    const char* sourceText) {

  if (!isStorageReady()) {
    return false;
  }

  File f = FFat.open(path, "r");
  if (!f) {
    return false;
  }

  gAmateurCatalogCount = 0;

  while (f.available() &&
         gAmateurCatalogCount < kAmateurCatalogMax) {

    String line = f.readStringUntil('\n');
    line.trim();

    if (line.length() < 3) continue;

    const int tab = line.indexOf('\t');
    if (tab <= 0) continue;

    const long id =
        line.substring(0, tab).toInt();

    String name =
        line.substring(tab + 1);

    name.trim();

    if (id <= 0 || name.length() == 0) {
      continue;
    }

    gAmateurCatalog[gAmateurCatalogCount].noradId = id;
    gAmateurCatalog[gAmateurCatalogCount].name = name;
    ++gAmateurCatalogCount;
  }

  f.close();

  if (gAmateurCatalogCount <= 0) {
    return false;
  }

  gAmateurCatalogStatus =
      String(sourceText) +
      ": " +
      String(gAmateurCatalogCount) +
      " satélites";

  return true;
}

static void keepOldCatalogStatus(
    const String& reason) {

  if (gAmateurCatalogCount > 0) {
    gAmateurCatalogStatus =
        reason +
        " · usando " +
        String(gAmateurCatalogCount) +
        " en memoria";
    return;
  }

  if (loadAmateurCatalogFromFile(
          kAmateurCacheFile,
          "Caché FFat")) {
    gAmateurCatalogStatus =
        reason +
        " · usando caché FFat (" +
        String(gAmateurCatalogCount) +
        ")";
    return;
  }

  gAmateurCatalogStatus = reason;
}

bool loadAmateurRadioCatalog() {
  clearSatRadioInspection();

  // Botón normal = NO descargar Internet otra vez.
  // 1) Si ya está en RAM, usarlo.
  if (gAmateurCatalogCount > 0) {
    gAmateurCatalogStatus =
        "En memoria: " +
        String(gAmateurCatalogCount) +
        " satélites";
    return true;
  }

  // 2) Si existe caché FFat, cargarla.
  if (loadAmateurCatalogFromFile(
          kAmateurCacheFile,
          "Caché FFat")) {
    return true;
  }

  // 3) Sólo si nunca hubo caché, intentar Internet.
  return refreshAmateurRadioCatalog();
}

bool refreshAmateurRadioCatalog() {
  clearSatRadioInspection();

  // Evitar explícitamente repetir la descarga en la misma sesión.
  if (gRemoteRefreshAttempted &&
      (millis() - gLastRemoteRefreshMs) <
          kCelestrakMinRefreshMs) {

    const unsigned long remainMs =
        kCelestrakMinRefreshMs -
        (millis() - gLastRemoteRefreshMs);

    const unsigned long remainMin =
        (remainMs + 59999UL) / 60000UL;

    keepOldCatalogStatus(
        "CelesTrak: esperar " +
        String(remainMin) +
        " min para actualizar"
    );

    return gAmateurCatalogCount > 0;
  }

  gRemoteRefreshAttempted = true;
  gLastRemoteRefreshMs = millis();

  if (WiFi.status() != WL_CONNECTED) {
    keepOldCatalogStatus("Sin WiFi");
    return false;
  }

  if (!isStorageReady()) {
    keepOldCatalogStatus("FFat no disponible");
    return false;
  }

  const char* url =
      "https://celestrak.org/NORAD/elements/"
      "gp.php?GROUP=AMATEUR&FORMAT=CSV";

  Serial.println("=== refreshAmateurRadioCatalog ===");

  HTTPClient http;
  http.begin(url);
  http.setConnectTimeout(3000);
  http.setTimeout(10000);

  const int code = http.GET();

  Serial.print("HTTP CelesTrak amateur: ");
  Serial.println(code);

  if (code != HTTP_CODE_OK) {
    http.end();

    if (code == 403) {
      keepOldCatalogStatus(
          "CelesTrak 403: límite de actualización"
      );
    } else {
      keepOldCatalogStatus(
          "Error CelesTrak HTTP " + String(code)
      );
    }

    return gAmateurCatalogCount > 0;
  }

  Stream* stream = http.getStreamPtr();

  if (stream == nullptr) {
    http.end();
    keepOldCatalogStatus("Sin stream CelesTrak");
    return gAmateurCatalogCount > 0;
  }

  stream->setTimeout(2500);

  String header = stream->readStringUntil('\n');
  header.trim();

  const int nameCol =
      csvColumnIndex(header, "OBJECT_NAME");

  const int noradCol =
      csvColumnIndex(header, "NORAD_CAT_ID");

  if (nameCol < 0 || noradCol < 0) {
    http.end();
    keepOldCatalogStatus("CSV CelesTrak desconocido");
    return gAmateurCatalogCount > 0;
  }

  FFat.remove(kAmateurTempFile);

  File tmp = FFat.open(kAmateurTempFile, "w");
  if (!tmp) {
    http.end();
    keepOldCatalogStatus("No se pudo crear caché temporal");
    return gAmateurCatalogCount > 0;
  }

  int downloadedCount = 0;
  unsigned long lastDataMs = millis();

  while ((http.connected() || stream->available()) &&
         downloadedCount < kAmateurCatalogMax) {

    if (!stream->available()) {
      if (millis() - lastDataMs > 3000UL) {
        break;
      }

      delay(2);
      continue;
    }

    String line = stream->readStringUntil('\n');
    lastDataMs = millis();
    line.trim();

    if (line.length() < 3) continue;

    String name = csvField(line, nameCol);
    String noradText = csvField(line, noradCol);

    name.trim();
    noradText.trim();

    const long id = noradText.toInt();

    if (id <= 0 || name.length() == 0) {
      continue;
    }

    name.replace("\t", " ");
    name.replace("\r", " ");
    name.replace("\n", " ");

    tmp.print(id);
    tmp.print('\t');
    tmp.println(name);

    ++downloadedCount;
  }

  tmp.flush();
  tmp.close();
  http.end();

  // Una descarga válida de este grupo contiene muchas entradas.
  // Si llega truncada, conservar SIEMPRE la caché anterior.
  if (downloadedCount < 20) {
    FFat.remove(kAmateurTempFile);
    keepOldCatalogStatus(
        "Descarga CelesTrak incompleta"
    );
    return gAmateurCatalogCount > 0;
  }

  // Sustituir la caché únicamente después de tener una descarga válida.
  FFat.remove(kAmateurCacheFile);

  if (!FFat.rename(
          kAmateurTempFile,
          kAmateurCacheFile)) {

    FFat.remove(kAmateurTempFile);
    keepOldCatalogStatus(
        "No se pudo guardar la nueva caché"
    );
    return gAmateurCatalogCount > 0;
  }

  // Y sólo AHORA sustituir el catálogo que está en RAM.
  if (!loadAmateurCatalogFromFile(
          kAmateurCacheFile,
          "CelesTrak actualizado")) {

    keepOldCatalogStatus(
        "Caché nueva inválida"
    );
    return false;
  }

  Serial.print("Catalogo Amateur actualizado: ");
  Serial.println(gAmateurCatalogCount);

  return true;
}

int getAmateurCatalogCount() {
  return gAmateurCatalogCount;
}

AmateurCatalogItem getAmateurCatalogItem(int index) {
  AmateurCatalogItem empty{0, ""};

  if (index < 0 || index >= gAmateurCatalogCount) {
    return empty;
  }

  return gAmateurCatalog[index];
}

bool hasAmateurCatalog() {
  return gAmateurCatalogCount > 0;
}

const char* getAmateurCatalogStatusText() {
  return gAmateurCatalogStatus.c_str();
}

void clearSatRadioInspection() {
  gInspection.checked = false;
  gInspection.queryOk = false;
  gInspection.noradId = 0;
  gInspection.name = "";

  gInspection.txTotal = 0;
  gInspection.txActive = 0;
  gInspection.txAmateur = 0;
  gInspection.txActiveAmateur = 0;
  gInspection.txTwoWay = 0;

  gInspection.modes = "";
  gInspection.downlinks = "";
  gInspection.uplinks = "";
  gInspection.types = "";
  gInspection.functions = "";
  gInspection.error = "";
}

SatRadioInspection getSatRadioInspection() {
  return gInspection;
}

bool inspectSatelliteRadio(
    long noradId,
    const String& name) {

  clearSatRadioInspection();

  gInspection.checked = true;
  gInspection.noradId = noradId;
  gInspection.name = name;

  if (noradId <= 0) {
    gInspection.error = "NORAD invalido";
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    gInspection.error = "WiFi no conectada";
    return false;
  }

  // Sin alive=true: necesitamos saber también si existen transmisores
  // amateur registrados pero actualmente inactivos.
  const String url =
      "https://db.satnogs.org/api/transmitters/"
      "?format=json&satellite__norad_cat_id=" +
      String(noradId);

  Serial.println("=== inspectSatelliteRadio ===");
  Serial.print("NORAD: ");
  Serial.println(noradId);

  HTTPClient http;
  http.begin(url);
  http.setConnectTimeout(3000);
  http.setTimeout(8000);

  const int code = http.GET();

  Serial.print("HTTP SatNOGS: ");
  Serial.println(code);

  if (code != HTTP_CODE_OK) {
    http.end();
    gInspection.error =
        "SatNOGS HTTP " + String(code);
    return false;
  }

  const String payload = http.getString();
  http.end();

  const char* json = payload.c_str();
  while (*json == ' ' || *json == '\r' || *json == '\n' || *json == '\t') ++json;
  if (*json != '[') {
    gInspection.error = "JSON SatNOGS invalido";
    return false;
  }

  const char* cursor = json + 1;
  while (*cursor != '\0') {
    const char* object = strchr(cursor, '{');
    if (object == nullptr) break;
    const char* objectEnd = searchObjectEnd(object);
    if (objectEnd == nullptr) break;
    cursor = objectEnd + 1;

    ++gInspection.txTotal;

    String status, service, type, mode, description;
    readObjectString(object, objectEnd, "\"status\"", status);
    readObjectString(object, objectEnd, "\"service\"", service);
    readObjectString(object, objectEnd, "\"type\"", type);
    readObjectString(object, objectEnd, "\"mode\"", mode);
    readObjectString(object, objectEnd, "\"description\"", description);

    const bool active =
        status.equalsIgnoreCase("active");

    const bool amateur =
        service.equalsIgnoreCase("Amateur");

    const bool hasUplink =
        hasPositiveJsonNumber(object, objectEnd, "\"uplink_low\"") ||
        hasPositiveJsonNumber(object, objectEnd, "\"uplink_high\"") ||
        hasPositiveJsonNumber(object, objectEnd, "\"uplink\"");

    const bool typeTwoWay =
        type.equalsIgnoreCase("Transceiver") ||
        type.equalsIgnoreCase("Transponder");

    const bool twoWay =
        active && amateur &&
        (hasUplink || typeTwoWay);

    if (active) {
      ++gInspection.txActive;
    }

    if (amateur) {
      ++gInspection.txAmateur;
    }

    if (active && amateur) {
      ++gInspection.txActiveAmateur;

      appendUnique(gInspection.modes, mode);
      appendUnique(gInspection.types, type);
      appendUnique(
          gInspection.functions,
          description,
          220
      );

      String dl =
          hzRangeToMHz(
              object, objectEnd,
              "\"downlink_low\"",
              "\"downlink_high\""
          );

      double singleFrequency = 0.0;
      if (dl.length() == 0 &&
          readObjectDouble(object, objectEnd, "\"downlink\"", singleFrequency) &&
          singleFrequency > 0.0) {
        dl =
            String(singleFrequency / 1000000.0, 4) +
            " MHz";
      }

      String ul =
          hzRangeToMHz(
              object, objectEnd,
              "\"uplink_low\"",
              "\"uplink_high\""
          );

      if (ul.length() == 0 &&
          readObjectDouble(object, objectEnd, "\"uplink\"", singleFrequency) &&
          singleFrequency > 0.0) {
        ul =
            String(singleFrequency / 1000000.0, 4) +
            " MHz";
      }

      appendUnique(gInspection.downlinks, dl);
      appendUnique(gInspection.uplinks, ul);
    }

    if (twoWay) {
      ++gInspection.txTwoWay;
    }
  }

  gInspection.queryOk = true;

  Serial.print("TX total/active/amateur/active-amateur/twoway: ");
  Serial.print(gInspection.txTotal);
  Serial.print("/");
  Serial.print(gInspection.txActive);
  Serial.print("/");
  Serial.print(gInspection.txAmateur);
  Serial.print("/");
  Serial.print(gInspection.txActiveAmateur);
  Serial.print("/");
  Serial.println(gInspection.txTwoWay);

  return true;
}
