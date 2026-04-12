#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "Arduino_JSON.h"
//#include "satellite_tracker.h"
#include "imu_sensor.h"
#include "app_state.h"
#include "config.h"
#include "rotor_positioning.h"
#include "sat_tracking.h"

static const uint32_t kPredictionRefreshMarginSec = 5;

void ensureTrackingData() {
  if (!enableseguirsat) return;

  // si no hay tabla, cargar ya
  if (tabunixtime[0] == 0) {
    getPositions();
    return;
  }

  const long idx = long(unixtime) - long(tabunixtime[0]);
  const long remaining = long(kPredictionWindowSec) - idx;

  // refrescar al final de la ventana o si quedamos fuera de rango
  if (remaining <= (long)kPredictionRefreshMarginSec || idx < 0 || idx >= (long)kPredictionWindowSec) {
    Serial.println("Refrescando prediccion...");
    getPositions();
  }
}

void tickEachSecond() {
  unixtime++;

  if (!simulationMode) {
    updateAntennaPosition();
  }

  time1 = millis();

  if (tabunixtime[0] != 0 && unixtime >= tabunixtime[0]) {
    const uint32_t index = unixtime - tabunixtime[0];
    if (index < kPredictionWindowSec) {
      satazimut = tabazimut[index];
      satelevat = tabelevat[index];
    }
  }

  ensureTrackingData();

  if (enableseguirsat) {
    // no seguir bajo horizonte
    if (satelevat > 0.0f) {
      // no relanzar si ya hay un movimiento/orientacion en curso
      if (!isAnyMotionBusy()) {
        if ((fabsf(antazimut - satazimut) > offazimut) ||
            (fabsf(antelevat - satelevat) > offelevat)) {
          orientAntenna(satazimut, satelevat);
        }
      }
    }
  }
}
