#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "Arduino_JSON.h"
#include "sat_visible_now.h"
#include "config.h"

static VisibleSatItem gVisible[32];
static int gVisibleCount = 0;

void clearVisibleNow() {
  gVisibleCount = 0;
  for (int i = 0; i < 32; ++i) {
    gVisible[i].noradId = 0;
    gVisible[i].name = "";
  }
}

int getVisibleNowCount() {
  return gVisibleCount;
}

VisibleSatItem getVisibleNowItem(int index) {
  VisibleSatItem empty{0, ""};
  if (index < 0 || index >= gVisibleCount) return empty;
  return gVisible[index];
}

bool searchVisibleNow() {
  clearVisibleNow();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("searchVisibleNow: WiFi no conectada");
    return false;
  }

  String url =
    String("https://api.n2yo.com/rest/v1/satellite/above/") +
    String(MY_LAT, 6) + "/" +
    String(MY_LON, 6) + "/" +
    String(MY_ALT_M) + "/90/18/&apiKey=" +
    String(N2YO_API_KEY);

  Serial.println("=== searchVisibleNow ===");
  Serial.println(url);

  HTTPClient http;
  http.begin(url);
  int code = http.GET();

  Serial.print("HTTP code above: ");
  Serial.println(code);

  if (code <= 0) {
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JSONVar root = JSON.parse(payload);
  if (JSON.typeof(root) != "object") return false;
  if (JSON.typeof(root["above"]) != "array") return false;

  JSONVar arr = root["above"];
  int n = arr.length();

  for (int i = 0; i < n && gVisibleCount < 32; ++i) {
    if (JSON.typeof(arr[i]["satid"]) == "undefined") continue;
    if (JSON.typeof(arr[i]["satname"]) == "undefined") continue;

    gVisible[gVisibleCount].noradId = long(arr[i]["satid"]);
    gVisible[gVisibleCount].name = String(arr[i]["satname"]);
    ++gVisibleCount;
  }

  Serial.print("Visibles ahora: ");
  Serial.println(gVisibleCount);

  return gVisibleCount > 0;
}

bool isVisibleNow(long noradId) {
  for (int i = 0; i < getVisibleNowCount(); ++i) {
    VisibleSatItem item = getVisibleNowItem(i);
    if (item.noradId == noradId) return true;
  }
  return false;
}
