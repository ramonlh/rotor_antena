

#include <Arduino.h>
#include "rotor_positioning.h"
#include "rotor_axes.h"
#include "rotor_io.h"
#include "imu_sensor.h"
#include "app_state.h"
#include "config.h"

static float normalizeAzimuth(float azim) {
  while (azim < 0.0f) azim += 360.0f;
  while (azim >= 360.0f) azim -= 360.0f;
  return azim;
}

static float clampElevation(float el) {
  if (el < 0.0f) return 0.0f;
  if (el > 90.0f) return 90.0f;
  return el;
}

static float angularDistanceDeg(float a, float b) {
  a = normalizeAzimuth(a);
  b = normalizeAzimuth(b);

  float distance = fabsf(a - b);

  if (distance > 180.0f) {
    distance = 360.0f - distance;
  }

  return distance;
}

static float signedAngularDeltaDeg(
    float fromAngle,
    float toAngle) {

  float delta =
      normalizeAzimuth(toAngle) -
      normalizeAzimuth(fromAngle);

  if (delta > 180.0f) {
    delta -= 360.0f;
  }

  if (delta < -180.0f) {
    delta += 360.0f;
  }

  return delta;
}

static float azLastSafetyAngle = 0.0f;
static unsigned long azLastProgressMs = 0;
static unsigned long azSafetyStartMs = 0;
static unsigned long azAllowedMoveMs = 0;

bool isOrientationBusy() {
  return orientMoveActive;
}

void stopMotionBySafety(const char* reason) {
  Serial.println();
  Serial.println("=================================");
  Serial.print("SEGURIDAD ROTOR: ");
  Serial.println(reason);
  Serial.println("Motores detenidos");
  Serial.println("Seguimiento cancelado");
  Serial.println("=================================");

  stopAllMotion();

  // Evitar que el seguimiento vuelva a arrancar
  // automáticamente después del fallo.
  enableseguirsat = false;
}

void updateSimulationMotion() {
  if (!simulationMode) return;

  unsigned long now = millis();
  if (simLastUpdateMs == 0) {
    simLastUpdateMs = now;
    return;
  }

  float dt = (now - simLastUpdateMs) / 1000.0f;
  simLastUpdateMs = now;

  float azStep = simAzSpeedDegPerSec * dt;
  float elStep = simElSpeedDegPerSec * dt;

  // azimut
  float azDiff = simTargetAz - angleZ;
  if (azDiff > 180.0f) azDiff -= 360.0f;
  if (azDiff < -180.0f) azDiff += 360.0f;

  if (fabsf(azDiff) <= azStep) {
    angleZ = simTargetAz;
    azMoveActive = false;
  } else {
    angleZ += (azDiff > 0 ? azStep : -azStep);
  }

  while (angleZ < 0.0f) angleZ += 360.0f;
  while (angleZ > 360.0f) angleZ -= 360.0f;

  // elevación
  float elDiff = simTargetEl - angleX;
  if (fabsf(elDiff) <= elStep) {
    angleX = simTargetEl;
    elevMoveActive = false;
  } else {
    angleX += (elDiff > 0 ? elStep : -elStep);
  }

  if (angleX < 0.0f) angleX = 0.0f;
  if (angleX > 90.0f) angleX = 90.0f;

  antazimut = angleZ;
  antelevat = angleX;
}

void startOrientationMove(float azim, float elev) {
  motionStoppedByUser = false;

  if (elev < 0.0f) elev = 0.0f;
  if (elev > 90.0f) elev = 90.0f;
  azim = normalizeAzimuth(azim);

  // cancelar orientación previa
  orientMoveActive = false;
  orientPhase = ORIENT_IDLE;

  orientTargetAz = azim;
  orientTargetEl = elev;

  // arranca siempre por elevación, también en simulación
  startElevationMove(orientTargetEl);
  orientMoveActive = true;
  orientPhase = ORIENT_WAIT_ELEV;
}

void updateOrientationMove() {
  if (!orientMoveActive) {
    return;
  }

  switch (orientPhase) {
    case ORIENT_WAIT_ELEV:
      if (!isElevationBusy()) {
        startAzimuthMove(orientTargetAz);
        orientPhase = ORIENT_WAIT_AZ;
      }
      break;

    case ORIENT_WAIT_AZ:
      if (!isAzimuthBusy()) {
        if (!simulationMode) {
          updateAntennaPosition();
        }
        antazimut = angleZ;
        antelevat = angleX;
        orientMoveActive = false;
        orientPhase = ORIENT_IDLE;
      }
      break;

    default:
      orientMoveActive = false;
      orientPhase = ORIENT_IDLE;
      break;
  }
}

bool isAzimuthBusy() {
  return azMoveActive;
}

void startAzimuthMove(float azim) {
  motionStoppedByUser = false;
  azim = normalizeAzimuth(azim);

  if (simulationMode) {
    Serial.printf("SIM: startAzimuthMove -> %.1f\n", azim);

    azTarget = azim;
    simTargetAz = azim;

    if (simLastUpdateMs == 0) {
      simLastUpdateMs = millis();
    }

    float azDiff = simTargetAz - angleZ;
    if (azDiff > 180.0f) azDiff -= 360.0f;
    if (azDiff < -180.0f) azDiff += 360.0f;

    if (fabsf(azDiff) <= 0.05f) {
      angleZ = simTargetAz;
      antazimut = angleZ;
      azMoveActive = false;
    } else {
      azMoveActive = true;
    }
    return;
  }

updateAntennaPosition();

azTarget = azim;
azMovePhase2 = false;

float distanceDeg =
    angularDistanceDeg(angleZ, azTarget);

if (distanceDeg <= angfrenadoazim) {
  disableRotation();
  antazimut = angleZ;
  azMoveActive = false;
  return;
}

  unsigned long now = millis();

  azMoveStartMs = now;
  azSafetyStartMs = now;
  azLastProgressMs = now;
  azLastSafetyAngle = angleZ;

  azAllowedMoveMs = calculateMoveTimeoutMs(
      distanceDeg,
      kMinAzSpeedDegPerSec
  );

  Serial.printf(
      "Azimut: %.1f -> %.1f, distancia %.1f, limite %lu ms\n",
      angleZ,
      azTarget,
      distanceDeg,
      azAllowedMoveMs
  );

  disableRotation();

  if (angleZ > 180.0f) {
    if (azTarget > 180.0f) {
      azMoveDirectionRight = (angleZ <= azTarget);
      azMovePhase2 = false;
    } else {
      if (angleZ > azTarget) {
        azMoveDirectionRight = true;
        azTarget = 360.0f;
        azMovePhase2 = false;
      } else {
        azMoveDirectionRight = false;
        azMovePhase2 = false;
      }
    }
  } else {
    if (azTarget <= 180.0f) {
      azMoveDirectionRight = (angleZ <= azTarget);
      azMovePhase2 = false;
    } else {
      if (angleZ < azTarget) {
        azMoveDirectionRight = false;
        azTarget = 0.0f;
        azMovePhase2 = false;
      } else {
        azMoveDirectionRight = true;
        azMovePhase2 = false;
      }
    }
  }

  if (azMoveDirectionRight) {
    digitalWrite(kGiro1pin, ON);
    digitalWrite(kGiro2pin, ON);
  } else {
    digitalWrite(kGiro1pin, OFF);
    digitalWrite(kGiro2pin, OFF);
  }
  digitalWrite(kGiropin, ON);
  azMoveActive = true;
}

void updateAzimuthMove() {
  if (!azMoveActive) {
    return;
  }

  if (simulationMode) {
    return;
  }

  updateAntennaPosition();

  if (azMoveDirectionRight) {
    if (!azMovePhase2) {
      if (angleZ >= (azTarget - angfrenadoazim)) {
        disableRotation();

        // caso especial: cruzar 360 -> destino real <= 180
        if (azTarget >= 359.0f) {
          delay(50);
          angleZ = offazimut + 5.0f;
          azTarget = normalizeAzimuth(antazimut); // provisional, no se usa
          azMovePhase2 = true;
          azMoveStartMs = millis();
          return;
        }

        antazimut = angleZ;
        azMoveActive = false;
      }
    } else {
      if (angleZ >= azTarget) {
        disableRotation();
        antazimut = angleZ;
        azMoveActive = false;
      }
    }
  } else {
    if (!azMovePhase2) {
      if (angleZ <= (azTarget + angfrenadoazim)) {
        disableRotation();

        // caso especial: cruzar 0 -> destino real > 180
        if (azTarget <= 1.0f) {
          delay(50);
          angleZ = 360.0f - offazimut - 5.0f;
          azMovePhase2 = true;
          azMoveStartMs = millis();
          return;
        }

        antazimut = angleZ;
        azMoveActive = false;
      }
    } else {
      if (angleZ <= azTarget) {
        disableRotation();
        antazimut = angleZ;
        azMoveActive = false;
      }
    }
  }

  // Si llegó al objetivo, no aplicar el watchdog.
  if (!azMoveActive) {
    return;
  }

  unsigned long now = millis();

  // Cambio angular desde la última posición válida.
  float delta = signedAngularDeltaDeg(
      azLastSafetyAngle,
      angleZ
  );

  bool correctProgress;

  if (azMoveDirectionRight) {
    correctProgress = delta >= kMinProgressDeg;
  } else {
    correctProgress = delta <= -kMinProgressDeg;
  }

  if (correctProgress) {
    azLastSafetyAngle = angleZ;
    azLastProgressMs = now;
  }

  // Sensor inmóvil, motor bloqueado o sentido incorrecto.
  if ((now - azLastProgressMs) >
      kNoProgressTimeoutMs) {

    stopMotionBySafety(
        "azimut sin progreso o sentido incorrecto"
    );

    return;
  }

  // Tiempo total excesivo para la distancia solicitada.
  if ((now - azSafetyStartMs) >
      azAllowedMoveMs) {

    stopMotionBySafety(
        "tiempo excesivo de azimut"
    );

    return;
  }
}

void setAzimuth(float azim) {
  startAzimuthMove(azim);

  while (isAzimuthBusy()) {
    updateAzimuthMove();
    delay(1);
  }
}

void orientAntenna(float azim, float elev) {
  startOrientationMove(azim, elev);
}

void jogAzimuth(float delta) {
  enableseguirsat = false;

  // Una orden manual de 5 grados debe terminar antes
  // de admitir otra.
  if (azMoveActive || orientMoveActive) {
    Serial.println(
        "JOG AZ ignorado: azimut todavía en movimiento"
    );
    return;
  }

  motionStoppedByUser = false;

  updateAntennaPosition();

  float startAz = angleZ;
  float newAz = normalizeAzimuth(startAz + delta);

  // Izquierda/derecha solo mueven azimut.
  disableElevation();
  elevMoveActive = false;

  orientMoveActive = false;
  orientPhase = ORIENT_IDLE;

  orientTargetAz = newAz;
  orientTargetEl = angleX;

  Serial.printf(
      "JOG AZ: inicio=%.1f, paso=%.1f, objetivo=%.1f\n",
      startAz,
      delta,
      newAz
  );

  startAzimuthMove(newAz);
}

void jogElevation(float delta) {
  enableseguirsat = false;

  if (elevMoveActive || orientMoveActive) {
    Serial.println(
        "JOG EL ignorado: elevacion todavía en movimiento"
    );
    return;
  }

  motionStoppedByUser = false;

  updateAntennaPosition();

  float startEl = angleX;
  float newEl = clampElevation(startEl + delta);

  // Arriba/abajo solo mueven elevación.
  disableRotation();
  azMoveActive = false;

  orientMoveActive = false;
  orientPhase = ORIENT_IDLE;

  orientTargetAz = angleZ;
  orientTargetEl = newEl;

  Serial.printf(
      "JOG EL: inicio=%.1f, paso=%.1f, objetivo=%.1f\n",
      startEl,
      delta,
      newEl
  );

  startElevationMove(newEl);
}

bool isAnyMotionBusy() {
  return isElevationBusy() || isAzimuthBusy() || isOrientationBusy();
}

void stopAllMotion() {
  disableElevation();
  disableRotation();
  motionStoppedByUser = true;
  elevMoveActive = false;
  azMoveActive = false;
  orientMoveActive = false;

  orientPhase = ORIENT_IDLE;

  // opcional pero útil: fijar la referencia actual como posición “alcanzada”
  antazimut = angleZ;
  antelevat = angleX;
  Serial.println("STOP: movimiento cancelado");
}

const char* getMotionStatusText() {
  if (orientMoveActive) {
    if (orientPhase == ORIENT_WAIT_ELEV) return "ORIENT_ELEV";
    if (orientPhase == ORIENT_WAIT_AZ)   return "ORIENT_AZ";
    return "ORIENT";
  }

  if (isElevationBusy()) return "MOV_ELEV";
  if (isAzimuthBusy())   return "MOV_AZ";

  return "IDLE";
}
