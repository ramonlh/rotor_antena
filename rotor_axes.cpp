

#include <Arduino.h>
#include "rotor_axes.h"
#include "rotor_positioning.h"
#include "rotor_io.h"
#include "imu_sensor.h"
#include "app_state.h"
#include "config.h"

static float clampElevation(float elev) {
  if (elev < 0.0f) elev = 0.0f;
  if (elev > 90.0f) elev = 90.0f;
  return elev;
}

static float elevLastSafetyAngle = 0.0f;
static unsigned long elevLastProgressMs = 0;
static unsigned long elevSafetyStartMs = 0;
static unsigned long elevAllowedMoveMs = 0;

bool isElevationBusy() {
  return elevMoveActive;
}

void startElevationMove(float elev) {
  motionStoppedByUser = false;
  elev = clampElevation(elev);

  if (simulationMode) {
    Serial.printf("SIM: startElevationMove -> %.1f\n", elev);

    elevTarget = elev;
    simTargetEl = elev;

    if (simLastUpdateMs == 0) {
      simLastUpdateMs = millis();
    }

    // Si ya estamos prácticamente en destino, no activar movimiento
    if (fabsf(angleX - simTargetEl) <= 0.05f) {
      angleX = simTargetEl;
      antelevat = angleX;
      elevMoveActive = false;
    } else {
      elevMoveActive = true;
    }
    return;
  }

updateAntennaPosition();
elevTarget = elev;

float distanceDeg = fabsf(elevTarget - angleX);

if (distanceDeg <= angfrenadoelev) {
  disableElevation();
  antelevat = angleX;
  elevMoveActive = false;
  return;
}

  unsigned long now = millis();

  elevMoveStartMs = now;
  elevSafetyStartMs = now;
  elevLastProgressMs = now;
  elevLastSafetyAngle = angleX;

  elevAllowedMoveMs = calculateMoveTimeoutMs(
      distanceDeg,
      kMinElSpeedDegPerSec
  );

  Serial.printf(
      "Elevacion: %.1f -> %.1f, distancia %.1f, limite %lu ms\n",
      angleX,
      elevTarget,
      distanceDeg,
      elevAllowedMoveMs
  );

  disableElevation();

  if (angleX < elevTarget) {
    digitalWrite(kElev1pin, ON);
    digitalWrite(kElev2pin, ON);
    elevMoveDirectionUp = true;
  } else {
    digitalWrite(kElev1pin, OFF);
    digitalWrite(kElev2pin, OFF);
    elevMoveDirectionUp = false;
  }

  enableElevation();
  elevMoveActive = true;
}

void updateElevationMove() {
  if (!elevMoveActive) {
    return;
  }

  if (simulationMode) {
    return;
  }

  updateAntennaPosition();

  // -------------------------------------------------------
  // Comprobar primero si ya llegó al destino
  // -------------------------------------------------------

  bool targetReached = false;

  if (elevMoveDirectionUp) {
    targetReached =
        angleX >= (elevTarget - angfrenadoelev);
  } else {
    targetReached =
        angleX <= (elevTarget + angfrenadoelev);
  }

  if (targetReached) {
    disableElevation();
    antelevat = angleX;
    elevMoveActive = false;
    return;
  }

  unsigned long now = millis();

  // -------------------------------------------------------
  // Comprobar avance en el sentido correcto
  // -------------------------------------------------------

  float delta = angleX - elevLastSafetyAngle;

  bool correctProgress;

  if (elevMoveDirectionUp) {
    correctProgress = delta >= kMinProgressDeg;
  } else {
    correctProgress = delta <= -kMinProgressDeg;
  }

  if (correctProgress) {
    elevLastSafetyAngle = angleX;
    elevLastProgressMs = now;
  }

  // -------------------------------------------------------
  // Sensor inmóvil, motor bloqueado o sentido incorrecto
  // -------------------------------------------------------

  if ((now - elevLastProgressMs) >
      kNoProgressTimeoutMs) {

    stopMotionBySafety(
        "elevacion sin progreso o sentido incorrecto"
    );

    return;
  }

  // -------------------------------------------------------
  // Tiempo total excesivo para el recorrido solicitado
  // -------------------------------------------------------

  if ((now - elevSafetyStartMs) >
      elevAllowedMoveMs) {

    stopMotionBySafety(
        "tiempo excesivo de elevacion"
    );

    return;
  }
}

void setElevation(float elev) {
  startElevationMove(elev);

  // compatibilidad con el código antiguo
  while (isElevationBusy()) {
    updateElevationMove();
    delay(1);
  }
}
