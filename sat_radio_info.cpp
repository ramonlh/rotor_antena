

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "Arduino_JSON.h"
#include "sat_radio_info.h"

struct SatTxInfo {
  String description;
  String downlink;
  String uplink;
  String mode;
  String service;
  String status;
};

static SatTxInfo gTx[12];
static int gTxCount = 0;
static long gTxNorad = 0;
static bool gTxLoaded = false;

static String gRadioSource = "none";
static int gLastRadioHttpCode = 0;

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

static String hzToMHzString(double hz) {
  if (hz <= 0.0) return "-";
  return String(hz / 1000000.0, 6) + " MHz";
}

// Fallback local mínimo
static bool loadLocalFallback(long noradId) {
  clearTxCache();
  gTxNorad = noradId;

  if (noradId == 25544) {
    gTx[0] = {"FM voice", "145.800 MHz", "145.200 MHz", "FM", "Voice", "active"};
    gTx[1] = {"Crossband repeater", "145.800 MHz", "437.800 MHz", "FM", "Repeater", "active"};
    gTx[2] = {"Packet/APRS", "145.825 MHz", "145.825 MHz", "AFSK/AX.25", "Packet", "active"};
    gTxCount = 3;
    gTxLoaded = true;
    return true;
  }

  if (noradId == 48274) {
    gTx[0] = {"Tiangong voice/telemetry", "-", "-", "Unknown", "Unknown", "unknown"};
    gTxCount = 1;
    gTxLoaded = true;
    return true;
  }

  if (noradId == 7530) {
    gTx[0] = {"Mode U/V beacon", "145.977500 MHz", "-", "CW", "Beacon", "active"};
    gTxCount = 1;
    gTxLoaded = true;
    return true;
  }

  if (noradId == 22825) {
    gTx[0] = {"Mode U CW Beacon", "435.795000 MHz", "-", "CW", "Beacon", "active"};
    gTxCount = 1;
    gTxLoaded = true;
    return true;
  }

  if (noradId == 27607) {
    gTx[0] = {"FM voice CTCSS 67 Hz", "436.795000 MHz", "145.850000 MHz", "FM", "Voice", "active"};
    gTxCount = 1;
    gTxLoaded = true;
    return true;
  }

  if (noradId == 39444) {
    gTx[0] = {"Linear transponder", "145.950000 MHz", "435.130000 MHz", "USB/LSB", "Amateur", "active"};
    gTxCount = 1;
    gTxLoaded = true;
    return true;
  }

  if (noradId == 43017) {
    gTx[0] = {"DUV telemetry", "145.960000 MHz", "-", "DUV", "Telemetry", "active"};
    gTxCount = 1;
    gTxLoaded = true;
    return true;
  }

  if (noradId == 43137) {
    gTx[0] = {"DUV telemetry", "145.880000 MHz", "-", "DUV", "Telemetry", "decayed"};
    gTxCount = 1;
    gTxLoaded = true;
    return true;
  }

  return false;
}

bool refreshRadioInfoForSat(long noradId) {
  clearTxCache();

  bool haveLocal = loadLocalFallback(noradId);

  Serial.println("=== refreshRadioInfoForSat ===");
  Serial.print("NORAD solicitado: ");
  Serial.println(noradId);
  Serial.print("Fallback local disponible: ");
  Serial.println(haveLocal ? "SI" : "NO");

  if (WiFi.status() != WL_CONNECTED) {
    gRadioSource = haveLocal ? "fallback local (sin WiFi)" : "sin datos (sin WiFi)";
    return haveLocal;
  }

  HTTPClient http;

  String url = "https://db.satnogs.org/api/transmitters/?format=json&alive=true&satellite__norad_cat_id=" + String(noradId);
  Serial.print("URL SatNOGS: ");
  Serial.println(url);

  http.begin(url);
  int code = http.GET();
  gLastRadioHttpCode = code;
  Serial.print("HTTP code: ");
  Serial.println(code);

  if (code <= 0) {
    http.end();
    gRadioSource = haveLocal ? "fallback local (HTTP fallo)" : "sin datos (HTTP fallo)";
    return haveLocal;
  }

  String payload = http.getString();
  http.end();

  Serial.print("Payload len: ");
  Serial.println(payload.length());

  JSONVar root = JSON.parse(payload);
  if (JSON.typeof(root) != "array") {
    gRadioSource = haveLocal ? "fallback local (JSON invalido)" : "sin datos (JSON invalido)";
    return haveLocal;
  }

  clearTxCache();
  gTxNorad = noradId;

  int n = root.length();
  Serial.print("Transmitters recibidos: ");
  Serial.println(n);

  for (int i = 0; i < n && gTxCount < 12; ++i) {
    JSONVar item = root[i];

    String description = JSON.typeof(item["description"]) != "undefined" ? String(item["description"]) : "";
    String mode        = JSON.typeof(item["mode"]) != "undefined" ? String(item["mode"]) : "";
    String service     = JSON.typeof(item["service"]) != "undefined" ? String(item["service"]) : "";
    String status      = JSON.typeof(item["status"]) != "undefined" ? String(item["status"]) : "";

    gTx[gTxCount].description = description;
    gTx[gTxCount].mode        = mode;
    gTx[gTxCount].service     = service;
    gTx[gTxCount].status      = status;

    if (JSON.typeof(item["downlink_low"]) != "undefined")
      gTx[gTxCount].downlink = hzToMHzString(double(item["downlink_low"]));
    else if (JSON.typeof(item["downlink"]) != "undefined")
      gTx[gTxCount].downlink = hzToMHzString(double(item["downlink"]));
    else
      gTx[gTxCount].downlink = "-";

    if (JSON.typeof(item["uplink_low"]) != "undefined")
      gTx[gTxCount].uplink = hzToMHzString(double(item["uplink_low"]));
    else if (JSON.typeof(item["uplink"]) != "undefined")
      gTx[gTxCount].uplink = hzToMHzString(double(item["uplink"]));
    else
      gTx[gTxCount].uplink = "-";

    Serial.print("TX ");
    Serial.print(gTxCount);
    Serial.print(": ");
    Serial.print(description);
    Serial.print(" | DL=");
    Serial.print(gTx[gTxCount].downlink);
    Serial.print(" | UL=");
    Serial.print(gTx[gTxCount].uplink);
    Serial.print(" | MODE=");
    Serial.println(mode);

    ++gTxCount;
  }

  gTxLoaded = (gTxCount > 0);

  Serial.print("TX matches finales: ");
  Serial.println(gTxCount);

  if (!gTxLoaded) {
    bool ok = loadLocalFallback(noradId);
    gRadioSource = ok ? "fallback local (sin TX online)" : "sin datos";
    return ok;
  }

  gRadioSource = "SatNOGS";
  return true;
}

String buildRadioInfoHtml(long noradId) {
  if (!gTxLoaded || gTxNorad != noradId) {
    refreshRadioInfoForSat(noradId);
  }

  String out;
  out += "<div class='radioinfo'>";
  out += "<strong>Radio</strong><br>";

  if (!gTxLoaded || gTxCount == 0) {
    out += "Sin datos de radio.<br></div>";
    return out;
  }

  for (int i = 0; i < gTxCount; ++i) {
    out += "- ";
    if (gTx[i].description.length()) out += gTx[i].description; else out += "TX";
    out += " | DL: " + gTx[i].downlink;
    out += " | UL: " + gTx[i].uplink;
    if (gTx[i].mode.length()) out += " | " + gTx[i].mode;
    if (gTx[i].service.length()) out += " | " + gTx[i].service;
    if (gTx[i].status.length()) out += " | " + gTx[i].status;
    out += "<br>";
  }

  out += "</div>";
  return out;
}

const char* getRadioInfoSourceText() {
  return gRadioSource.c_str();
}

int getLastRadioHttpCode() {
  return gLastRadioHttpCode;
}
