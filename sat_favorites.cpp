#include <Arduino.h>
#include <FFat.h>
#include "Arduino_JSON.h"
#include "sat_favorites.h"
#include "storage_services.h"
#include "sat_catalog.h"

static const char* kFavoritesFile = "/favorites.json";

static SatFavoriteItem gFavorites[32];
static int gFavoritesCount = 0;

static void clearFavorites() {
  gFavoritesCount = 0;
  for (int i = 0; i < 32; ++i) {
    gFavorites[i].noradId = 0;
    gFavorites[i].name = "";
  }
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

  JSONVar root = JSON.parse(content);
  if (JSON.typeof(root) != "array") {
    Serial.println("favorites.json no es array");
    return false;
  }

  int n = root.length();
  for (int i = 0; i < n && gFavoritesCount < 32; ++i) {
    JSONVar item = root[i];
    if (JSON.typeof(item["norad"]) == "undefined") continue;
    if (JSON.typeof(item["name"]) == "undefined") continue;

    gFavorites[gFavoritesCount].noradId = long(item["norad"]);
    gFavorites[gFavoritesCount].name = String(item["name"]);
    ++gFavoritesCount;
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
    f.print(jsonEscape(gFavorites[i].name));
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

  if (!FFat.exists(kFavoritesFile)) {
    Serial.println("Creando favorites.json inicial desde catalogo base");
    clearFavorites();

    for (size_t i = 0; i < kSatCatalogCount && gFavoritesCount < 32; ++i) {
      gFavorites[gFavoritesCount].noradId = kSatCatalog[i].noradId;
      gFavorites[gFavoritesCount].name = kSatCatalog[i].name;
      ++gFavoritesCount;
    }

    return saveSatFavorites();
  }

  return loadSatFavorites();
}

int getSatFavoritesCount() {
  return gFavoritesCount;
}

SatFavoriteItem getSatFavorite(int index) {
  SatFavoriteItem empty{0, ""};
  if (index < 0 || index >= gFavoritesCount) return empty;
  return gFavorites[index];
}

bool hasSatFavorites() {
  return gFavoritesCount > 0;
}

bool addSatFavorite(long noradId, const String& name) {
  if (!isStorageReady() && !initStorage()) {
    return false;
  }

  // Si aún no hay favoritos cargados, intenta cargarlos desde FFat
  if (gFavoritesCount == 0) {
    loadSatFavorites();
  }

  // Si sigue vacío, inicializa desde el catálogo base
  if (gFavoritesCount == 0) {
    for (size_t i = 0; i < kSatCatalogCount && gFavoritesCount < 32; ++i) {
      gFavorites[gFavoritesCount].noradId = kSatCatalog[i].noradId;
      gFavorites[gFavoritesCount].name = kSatCatalog[i].name;
      ++gFavoritesCount;
    }
  }

  for (int i = 0; i < gFavoritesCount; ++i) {
    if (gFavorites[i].noradId == noradId) return true;
  }

  if (gFavoritesCount >= 32) return false;

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

String buildFavoriteOptionsHtml(long selectedSatId) {
  String out;
  for (int i = 0; i < gFavoritesCount; ++i) {
    out += "<option value='";
    out += String(gFavorites[i].noradId);
    out += "'";
    if (gFavorites[i].noradId == selectedSatId) out += " selected";
    out += ">";
    out += gFavorites[i].name;
    out += "</option>";
  }
  return out;
}