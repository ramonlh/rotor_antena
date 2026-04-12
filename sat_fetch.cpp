

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "Arduino_JSON.h"
#include "sat_fetch.h"
#include "app_state.h"
#include "config.h"

void getPositions() {
  timegetpositions = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("getPositions: WiFi no conectada");
    lastresponse = -1;
    satazimut = 0.0f;
    satelevat = 0.0f;
    satname = "";
    return;
  }

  HTTPClient http;

  const String serverPath =
      String(SERVER_POS_BASE) + String(satid) + SERVER_POS_MYDATA + String(kPredictionWindowSec) + SERVER_POS_KEY;

  Serial.println("=== getPositions ===");
  Serial.println(serverPath);

  http.begin(serverPath.c_str());
  const int httpResponseCode = http.GET();
  lastresponse = httpResponseCode;

  if (httpResponseCode <= 0) {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
    http.end();
    satazimut = 0.0f;
    satelevat = 0.0f;
    satname = "";
    return;
  }

  const String payload = http.getString();
  http.end();
Serial.println("----- RAW PAYLOAD BEGIN -----");
Serial.println(payload);
Serial.println("----- RAW PAYLOAD END -----");
  Serial.print("payload len: ");
  Serial.println(payload.length());
  Serial.println(payload.substring(0, 300));
Serial.print("Primer caracter payload: ");
if (payload.length() > 0) {
  Serial.println(payload[0]);
} else {
  Serial.println("(vacio)");
}
  JSONVar myObject = JSON.parse(payload);
  if (JSON.typeof(myObject) != "object") {
    Serial.println("JSON invalido");
    lastresponse = -3;
    satazimut = 0.0f;
    satelevat = 0.0f;
    satname = "";
    return;
  }

  if (JSON.typeof(myObject["info"]) == "undefined" ||
      JSON.typeof(myObject["positions"]) != "array") {
    Serial.println("JSON sin info/positions");
    lastresponse = -3;
    satazimut = 0.0f;
    satelevat = 0.0f;
    satname = "";
    return;
  }

  satname = String(myObject["info"]["satname"]);

  for (int i = 0; i < kTableSize; i++) {
    tabunixtime[i] = 0;
    tabazimut[i] = 0.0f;
    tabelevat[i] = 0.0f;
  }

  const int positionsLength = myObject["positions"].length();
  const int count = min(positionsLength, kTableSize);

  Serial.print("positionsLength: ");
  Serial.println(positionsLength);

  for (int i = 0; i < count; i++) {
    tabunixtime[i] = uint32_t(myObject["positions"][i]["timestamp"]);
    tabazimut[i] = double(myObject["positions"][i]["azimuth"]);
    tabelevat[i] = double(myObject["positions"][i]["elevation"]);
  }

  if (count > 0) {
    unixtime = tabunixtime[0] - 1;
    satazimut = tabazimut[0];
    satelevat = tabelevat[0];

    Serial.print("satname: ");
    Serial.println(satname);
    Serial.print("satazimut: ");
    Serial.println(satazimut);
    Serial.print("satelevat: ");
    Serial.println(satelevat);
  } else {
    Serial.println("Sin posiciones para este satelite");
    Serial.println(payload);

    lastresponse = -2;
    unixtime = 0;
    satazimut = 0.0f;
    satelevat = 0.0f;
    satname = "";
  }
}