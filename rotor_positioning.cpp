

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

bool isOrientationBusy() {
  return orientMoveActive;
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
  azMoveStartMs = millis();
  azMovePhase2 = false;

  if (abs(angleZ - azTarget) <= angfrenadoazim) {
    disableRotation();
    antazimut = angleZ;
    azMoveActive = false;
    return;
  }

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

  if ((millis() - azMoveStartMs) > kMoveTimeoutMs) {
    Serial.println("Azimuth timeout");
    disableRotation();
    azMoveActive = false;
    return;
  }

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
  // mando manual => cancelar seguimiento automático
  enableseguirsat = false;
  motionStoppedByUser = false;

  // tomar como base el objetivo ya pendiente si hay una orientación en curso;
  // si no, la posición actual real
  float baseAz = orientMoveActive ? orientTargetAz : angleZ;
  float baseEl = orientMoveActive ? orientTargetEl : angleX;

  float newAz = normalizeAzimuth(baseAz + delta);
  float newEl = clampElevation(baseEl);

  orientAntenna(newAz, newEl);
}

void jogElevation(float delta) {
  // mando manual => cancelar seguimiento automático
  enableseguirsat = false;
  motionStoppedByUser = false;

  // tomar como base el objetivo ya pendiente si hay una orientación en curso;
  // si no, la posición actual real
  float baseAz = orientMoveActive ? orientTargetAz : angleZ;
  float baseEl = orientMoveActive ? orientTargetEl : angleX;

  float newAz = normalizeAzimuth(baseAz);
  float newEl = clampElevation(baseEl + delta);

  orientAntenna(newAz, newEl);
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
