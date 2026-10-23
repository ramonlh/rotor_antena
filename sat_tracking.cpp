#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
//#include "satellite_tracker.h"
#include "imu_sensor.h"
#include "app_state.h"
#include "config.h"
#include "config_motion.h"
#include "rotor_positioning.h"
#include "sat_tracking.h"
#include "sat_fetch.h"
#include "diagnostic_log.h"

static const uint32_t kPredictionRefreshMarginSec = 5;
static const unsigned long kPredictionRetryMs = 15000UL;
static unsigned long gLastPredictionRequestMs = 0;

void ensureTrackingData() {
  // La posición del satélite seleccionado se mantiene actualizada SIEMPRE,
  // aunque no esté visible o aunque el seguimiento esté detenido.
  //
  // enableseguirsat sólo decide si el rotor debe moverse, no si debemos
  // conocer AZ/EL del satélite.

  if (satid <= 0) return;

  const unsigned long nowMs = millis();

  // Evitar reintentos HTTP cada segundo si N2YO/WiFi falla.
  const bool retryAllowed =
      gLastPredictionRequestMs == 0 ||
      (nowMs - gLastPredictionRequestMs) >= kPredictionRetryMs;

  // Si no hay tabla, intentar cargarla.
  if (tabunixtime[0] == 0) {
    if (retryAllowed) {
      gLastPredictionRequestMs = nowMs;
      getPositions();
    }
    return;
  }

  const long idx =
      long(unixtime) - long(tabunixtime[0]);

  const long remaining =
      long(kPredictionWindowSec) - idx;

  // Renovar la tabla antes de agotarla, tanto si se está siguiendo
  // como si sólo estamos mostrando la posición del satélite.
  if (remaining <= (long)kPredictionRefreshMarginSec ||
      idx < 0 ||
      idx >= (long)kPredictionWindowSec) {
    if (retryAllowed) {
      gLastPredictionRequestMs = nowMs;
      Serial.println("Refrescando posicion satelite...");
      getPositions();
    }
  }
}

void tickEachSecond() {
  unixtime++;

  if (!simulationMode) {
    updateAntennaPosition();
  }

  time1 = millis();

  bool targetCurrentValid = false;

  if (tabunixtime[0] != 0 && unixtime >= tabunixtime[0]) {
    const uint32_t index = unixtime - tabunixtime[0];
    if (index < kPredictionWindowSec) {
      satazimut = tabazimut[index];
      satelevat = tabelevat[index];
      targetCurrentValid = true;
    }
  }

  // Una tabla agotada no debe permanecer durante horas aparentando ser una
  // prediccion util. Invalidarla conserva por separado la edad del ultimo
  // éxito para diagnostico, pero impide mostrar/mover con coordenadas viejas.
  if (tabunixtime[0] != 0 &&
      unixtime >= tabunixtime[0] + kPredictionWindowSec) {
    tabunixtime[0] = 0;
    satazimut = 0.0f;
    satelevat = 0.0f;
    if (enableseguirsat) {
      enableseguirsat = false;
      stopAllMotion();
      addDiagnosticEvent("Seguimiento detenido: prediccion caducada");
    }
  }

  // La posición calculada cada segundo es la autoridad para el satélite
  // que se está siguiendo. En cuanto baja del mínimo operativo deja de
  // ser visible para el rotor: cancelar seguimiento y detener el movimiento.
  if (enableseguirsat &&
      targetCurrentValid &&
      satelevat < kMinElevationDeg) {
    enableseguirsat = false;
    stopAllMotion();
    addDiagnosticEvent("Seguimiento finalizado: satelite no visible");
    Serial.println("TRACK FIN: NO VISIBLE");
    // NO retornar: aunque se detenga el seguimiento, la posición
    // del satélite debe seguir calculándose y mostrándose.
  }

  ensureTrackingData();

  if (enableseguirsat &&
      targetCurrentValid &&
      satelevat >= kMinElevationDeg) {
    // No relanzar si ya hay un movimiento/orientación en curso.
    if (!isAnyMotionBusy()) {
      if ((fabsf(antazimut - satazimut) > offazimut) ||
          (fabsf(antelevat - satelevat) > offelevat)) {
        orientAntenna(satazimut, satelevat);
      }
    }
  }
}
