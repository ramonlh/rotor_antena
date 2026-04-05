#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "Arduino_JSON.h"
#include "satellite_tracker.h"
#include "imu_sensor.h"
#include "app_state.h"
#include "config.h"
#include "rotor_positioning.h"


void tickEachSecond() {
  unixtime++;
  updateAntennaPosition();
  time1 = millis();

  if (tabunixtime[0] != 0 && unixtime >= tabunixtime[0]) {
    const uint32_t index = unixtime - tabunixtime[0];
    if (index < kTableSize) {
      satazimut = tabazimut[index];
      satelevat = tabelevat[index];
    }
  }

  if (enableseguirsat) {
    if ((abs(antazimut - satazimut) > offazimut) || (abs(antelevat - satelevat) > offelevat)) {
      orientAntenna(satazimut, satelevat);
    }
  }
}

void getPositions() {
  timegetpositions = millis();
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTPClient http;
  const String serverPath = String(SERVER_POS_BASE) + String(satid) + SERVER_POS_MYDATA + String(kTableSize) + SERVER_POS_KEY;
  Serial.println(serverPath);
  http.begin(serverPath.c_str());
  const int httpResponseCode = http.GET();
  lastresponse = httpResponseCode;

  if (httpResponseCode > 0) {
    const String payload = http.getString();
    JSONVar myObject = JSON.parse(payload);
    satname = String(myObject["info"]["satname"]);

    const int positionsLength = myObject["positions"].length();
    const int count = min(positionsLength, kTableSize);
    for (int i = 0; i < count; i++) {
      tabunixtime[i] = uint32_t(myObject["positions"][i]["timestamp"]);
      tabazimut[i] = double(myObject["positions"][i]["azimuth"]);
      tabelevat[i] = double(myObject["positions"][i]["elevation"]);
    }

    unixtime = tabunixtime[0];
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}
