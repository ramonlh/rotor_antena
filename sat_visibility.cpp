#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "Arduino_JSON.h"
#include "sat_visibility.h"
#include "sat_favorites.h"
#include "config.h"

static SatVisibilityInfo gVis[32];
static int gVisCount = 0;

static void clearEntry(int i) {
  gVis[i].noradId = 0;
  gVis[i].visibleNow = false;
  gVis[i].hasFuturePass = false;
  gVis[i].nextPassStartUTC = 0;
  gVis[i].nextPassMaxEl = 0.0f;
}

void clearVisibilityCache() {
  gVisCount = 0;
  for (int i = 0; i < 32; ++i) clearEntry(i);
}

static int ensureEntry(long noradId) {
  for (int i = 0; i < gVisCount; ++i) {
    if (gVis[i].noradId == noradId) return i;
  }
  if (gVisCount >= 32) return -1;
  clearEntry(gVisCount);
  gVis[gVisCount].noradId = noradId;
  ++gVisCount;
  return gVisCount - 1;
}

bool isFavoriteVisibleNow(long noradId) {
  for (int i = 0; i < gVisCount; ++i) {
    if (gVis[i].noradId == noradId) return gVis[i].visibleNow;
  }
  return false;
}

bool hasFavoriteFuturePass(long noradId) {
  for (int i = 0; i < gVisCount; ++i) {
    if (gVis[i].noradId == noradId) return gVis[i].hasFuturePass;
  }
  return false;
}

uint32_t getFavoriteNextPassUTC(long noradId) {
  for (int i = 0; i < gVisCount; ++i) {
    if (gVis[i].noradId == noradId) return gVis[i].nextPassStartUTC;
  }
  return 0;
}

float getFavoriteNextPassMaxEl(long noradId) {
  for (int i = 0; i < gVisCount; ++i) {
    if (gVis[i].noradId == noradId) return gVis[i].nextPassMaxEl;
  }
  return 0.0f;
}

bool refreshVisibleNow() {
  if (WiFi.status() != WL_CONNECTED) return false;

  for (int i = 0; i < gVisCount; ++i) gVis[i].visibleNow = false;

  String url =
    String("https://api.n2yo.com/rest/v1/satellite/above/") +
    String(MY_LAT, 6) + "/" +
    String(MY_LON, 6) + "/" +
    String(MY_ALT_M) + "/90/18/&apiKey=" +
    String(N2YO_API_KEY);

  HTTPClient http;
  http.begin(url);
  int code = http.GET();
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

  for (int i = 0; i < n; ++i) {
    long norad = long(arr[i]["satid"]);
    int idx = ensureEntry(norad);
    if (idx >= 0) gVis[idx].visibleNow = true;
  }

  return true;
}

bool refreshFutureVisibility(uint8_t daysAhead, uint8_t minElevation) {
  if (WiFi.status() != WL_CONNECTED) return false;

  int favCount = getSatFavoritesCount();
  bool anyOk = false;

  for (int i = 0; i < favCount; ++i) {
    SatFavoriteItem item = getSatFavorite(i);
    int idx = ensureEntry(item.noradId);
    if (idx < 0) continue;

    gVis[idx].hasFuturePass = false;
    gVis[idx].nextPassStartUTC = 0;
    gVis[idx].nextPassMaxEl = 0.0f;

    String url =
      String("https://api.n2yo.com/rest/v1/satellite/radiopasses/") +
      String(item.noradId) + "/" +
      String(MY_LAT, 6) + "/" +
      String(MY_LON, 6) + "/" +
      String(MY_ALT_M) + "/" +
      String(daysAhead) + "/" +
      String(minElevation) +
      "/&apiKey=" + String(N2YO_API_KEY);

    HTTPClient http;
    http.begin(url);
    int code = http.GET();
    if (code <= 0) {
      http.end();
      continue;
    }

    String payload = http.getString();
    http.end();

    JSONVar root = JSON.parse(payload);
    if (JSON.typeof(root) != "object") continue;
    if (JSON.typeof(root["passes"]) != "array") continue;

    JSONVar passes = root["passes"];
    if (passes.length() <= 0) continue;

    gVis[idx].hasFuturePass = true;
    gVis[idx].nextPassStartUTC = uint32_t(passes[0]["startUTC"]);
    gVis[idx].nextPassMaxEl = double(passes[0]["maxEl"]);
    anyOk = true;
  }

  return anyOk;
}