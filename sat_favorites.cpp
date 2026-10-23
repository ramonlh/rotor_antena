#include <Arduino.h>
#include <FFat.h>
#include "sat_favorites.h"
#include "storage_services.h"
#include "sat_catalog.h"
#include "sat_visible_now.h"

static const char* kFavoritesFile = "/favorites.json";

static SatFavoriteItem gFavorites[32];
static int gFavoritesCount = 0;

static bool readFavoriteObject(
    const char* object,
    const char* objectEnd,
    long& norad,
    String& name) {
  const char* idKey = strstr(object, "\"norad\"");
  const char* nameKey = strstr(object, "\"name\"");
  if (idKey == nullptr || idKey >= objectEnd ||
      nameKey == nullptr || nameKey >= objectEnd) return false;

  const char* idColon = strchr(idKey, ':');
  char* idEnd = nullptr;
  norad = idColon != nullptr ? strtol(idColon + 1, &idEnd, 10) : 0;
  if (idColon == nullptr || idEnd == idColon + 1 || norad <= 0) return false;

  const char* nameColon = strchr(nameKey, ':');
  const char* quote = nameColon != nullptr ? strchr(nameColon, '\"') : nullptr;
  if (quote == nullptr || quote >= objectEnd) return false;

  name = "";
  for (const char* p = quote + 1; p < objectEnd; ++p) {
    if (*p == '\"') return true;
    if (*p == '\\' && p + 1 < objectEnd) ++p;
    name += *p;
  }
  name = "";
  return false;
}

static void clearFavorites() {
  gFavoritesCount = 0;
  for (int i = 0; i < 32; ++i) {
    gFavorites[i].noradId = 0;
    gFavorites[i].name = "";
  }
}

static String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);

  for (size_t i = 0; i < s.length(); ++i) {
    switch (s[i]) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '\"': out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += s[i];     break;
    }
  }
  return out;
}

static String jsonEscape(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '\"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

bool loadSatFavorites() {
  clearFavorites();

  if (!isStorageReady()) {
    Serial.println("FFat no listo en loadSatFavorites");
    return false;
  }

  if (!FFat.exists(kFavoritesFile)) {
    Serial.println("favorites.json no existe");
    return false;
  }

  File f = FFat.open(kFavoritesFile, "r");
  if (!f) {
    Serial.println("No se pudo abrir favorites.json");
    return false;
  }

  String content = f.readString();
  f.close();

  const char* json = content.c_str();
  while (*json == ' ' || *json == '\r' || *json == '\n' || *json == '\t') ++json;
  if (*json != '[') {
    Serial.println("favorites.json no es array");
    return false;
  }

  const char* cursor = json + 1;
  while (*cursor != '\0' && gFavoritesCount < 32) {
    const char* object = strchr(cursor, '{');
    if (object == nullptr) break;
    const char* objectEnd = strchr(object, '}');
    if (objectEnd == nullptr) break;
    long norad = 0;
    String name;
    if (readFavoriteObject(object, objectEnd, norad, name)) {
      gFavorites[gFavoritesCount].noradId = norad;
      gFavorites[gFavoritesCount].name = name;
      ++gFavoritesCount;
    }
    cursor = objectEnd + 1;
  }

  Serial.print("Favoritos cargados: ");
  Serial.println(gFavoritesCount);

  return gFavoritesCount > 0;
}

bool saveSatFavorites() {
  if (!isStorageReady()) {
    Serial.println("FFat no listo en saveSatFavorites");
    return false;
  }

  File f = FFat.open(kFavoritesFile, "w");
  if (!f) {
    Serial.println("No se pudo escribir favorites.json");
    return false;
  }

  f.print("[");

  for (int i = 0; i < gFavoritesCount; ++i) {
    if (i > 0) f.print(",");

    f.print("{\"norad\":");
    f.print(gFavorites[i].noradId);
    f.print(",\"name\":\"");
    f.print(jsonEscape(String(gFavorites[i].name.c_str())));
    f.print("\"}");
  }

  f.print("]");
  f.close();

  Serial.println("Favoritos guardados");
  return true;
}

bool initSatFavorites() {
  if (!isStorageReady() && !initStorage()) {
    return false;
  }

  bool ok = false;

  if (!FFat.exists(kFavoritesFile)) {
    Serial.println(
        "Creando favorites.json inicial desde catalogo base"
    );

    clearFavorites();

    for (size_t i = 0;
         i < kSatCatalogCount && gFavoritesCount < 32;
         ++i) {

      gFavorites[gFavoritesCount].noradId =
          kSatCatalog[i].noradId;
      gFavorites[gFavoritesCount].name =
          kSatCatalog[i].name;

      ++gFavoritesCount;
    }

    ok = saveSatFavorites();
  } else {
    ok = loadSatFavorites();
  }

  // La caché de visibilidad empieza a mantenerse sola desde el arranque.
  startVisibleNowAutoRefresh();

  return ok;
}

int getSatFavoritesCount() {
  return gFavoritesCount;
}

SatFavoriteItem getSatFavorite(int index) {
  SatFavoriteItem empty{0, ""};

  if (index < 0 || index >= gFavoritesCount) {
    return empty;
  }

  return gFavorites[index];
}

bool hasSatFavorites() {
  return gFavoritesCount > 0;
}

bool addSatFavorite(long noradId, const String& name) {
  if (!isStorageReady() && !initStorage()) {
    return false;
  }

  if (gFavoritesCount == 0) {
    loadSatFavorites();
  }

  if (gFavoritesCount == 0) {
    for (size_t i = 0;
         i < kSatCatalogCount && gFavoritesCount < 32;
         ++i) {

      gFavorites[gFavoritesCount].noradId =
          kSatCatalog[i].noradId;
      gFavorites[gFavoritesCount].name =
          kSatCatalog[i].name;

      ++gFavoritesCount;
    }
  }

  for (int i = 0; i < gFavoritesCount; ++i) {
    if (gFavorites[i].noradId == noradId) {
      return true;
    }
  }

  if (gFavoritesCount >= 32) {
    return false;
  }

  gFavorites[gFavoritesCount].noradId = noradId;
  gFavorites[gFavoritesCount].name = name;
  ++gFavoritesCount;

  return saveSatFavorites();
}

bool removeSatFavorite(long noradId) {
  int pos = -1;

  for (int i = 0; i < gFavoritesCount; ++i) {
    if (gFavorites[i].noradId == noradId) {
      pos = i;
      break;
    }
  }

  if (pos < 0) return false;

  for (int i = pos; i < gFavoritesCount - 1; ++i) {
    gFavorites[i] = gFavorites[i + 1];
  }

  --gFavoritesCount;
  return saveSatFavorites();
}

static void appendFavoriteOption(
    String& out,
    int index,
    long selectedSatId) {

  const SatVisibleState state =
      getVisibleNowState(gFavorites[index].noradId);

  const String safeName =
      htmlEscape(String(gFavorites[index].name.c_str()));

  out += "<option value='";
  out += String(gFavorites[index].noradId);

  out += "' data-name='";
  out += safeName;

  out += "' data-order='";
  out += String(index);

  out += "' data-state='";
  out += String((int)state);

  out += "' data-visible='";

  if (state == SAT_VIS_VISIBLE) out += "1";
  else if (state == SAT_VIS_NOT_VISIBLE) out += "0";
  else out += "?";

  out += "'";

  if (gFavorites[index].noradId == selectedSatId) {
    out += " selected";
  }

  out += ">";

  if (state == SAT_VIS_VISIBLE) {
    out += "&#x1F7E2; ";
    out += safeName;
    out += " - VISIBLE";
  } else if (state == SAT_VIS_NOT_VISIBLE) {
    out += "&#x26AA; ";
    out += safeName;
    out += " - NO";
  } else {
    out += "&#x1F7E1; ";
    out += safeName;
    out += " - COMPROBANDO";
  }

  out += "</option>";
}

String buildFavoriteOptionsHtml(long selectedSatId) {
  String out;
  out.reserve(gFavoritesCount * 110);

  // Visibles primero, luego desconocidos y por último los no visibles.
  const SatVisibleState order[3] = {
    SAT_VIS_VISIBLE,
    SAT_VIS_UNKNOWN,
    SAT_VIS_NOT_VISIBLE
  };

  for (int pass = 0; pass < 3; ++pass) {
    for (int i = 0; i < gFavoritesCount; ++i) {
      if (getVisibleNowState(gFavorites[i].noradId) !=
          order[pass]) {
        continue;
      }

      appendFavoriteOption(out, i, selectedSatId);
    }
  }

  return out;
}
