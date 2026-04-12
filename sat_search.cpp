#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "Arduino_JSON.h"
#include "sat_search.h"

static SatSearchItem gSearchResults[20];
static int gSearchCount = 0;

void clearSatSearchResults() {
  gSearchCount = 0;
  for (int i = 0; i < 20; ++i) {
    gSearchResults[i].noradId = 0;
    gSearchResults[i].name = "";
  }
}

int getSatSearchResultCount() {
  return gSearchCount;
}

SatSearchItem getSatSearchResult(int index) {
  SatSearchItem empty{0, ""};
  if (index < 0 || index >= gSearchCount) return empty;
  return gSearchResults[index];
}

static String urlEncodeSpaces(const String& s) {
  String out = s;
  out.replace(" ", "%20");
  return out;
}

bool searchSatellitesByName(const String& query) {
  clearSatSearchResults();

  String q = query;
  q.trim();
  if (q.length() < 2) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  String url =
    "https://celestrak.org/satcat/records.php?NAME=" + urlEncodeSpaces(q) +
    "&PAYLOADS=1&ACTIVE=1&ONORBIT=1&MAX=20&FORMAT=JSON";

  Serial.println("=== searchSatellitesByName ===");
  Serial.print("URL: ");
  Serial.println(url);

  HTTPClient http;
  http.begin(url);
  int code = http.GET();

  Serial.print("HTTP code: ");
  Serial.println(code);

  if (code <= 0) {
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  Serial.print("Payload len: ");
  Serial.println(payload.length());

  JSONVar root = JSON.parse(payload);
  if (JSON.typeof(root) != "array") {
    Serial.println("JSON no es array");
    return false;
  }

  int n = root.length();
  Serial.print("Resultados recibidos: ");
  Serial.println(n);

  for (int i = 0; i < n && gSearchCount < 20; ++i) {
    JSONVar item = root[i];

    if (JSON.typeof(item["NORAD_CAT_ID"]) == "undefined") continue;
    if (JSON.typeof(item["OBJECT_NAME"]) == "undefined") continue;

    gSearchResults[gSearchCount].noradId = long(item["NORAD_CAT_ID"]);
    gSearchResults[gSearchCount].name = String(item["OBJECT_NAME"]);
    ++gSearchCount;
  }

  Serial.print("Resultados guardados: ");
  Serial.println(gSearchCount);

  return gSearchCount > 0;
}