

#include <Arduino.h>
#include "rotor_axes.h"
#include "rotor_io.h"
#include "imu_sensor.h"
#include "app_state.h"
#include "config.h"

static float clampElevation(float elev) {
  if (elev < 0.0f) elev = 0.0f;
  if (elev > 90.0f) elev = 90.0f;
  return elev;
}

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
  elevMoveStartMs = millis();

  if (abs(angleX - elevTarget) <= angfrenadoelev) {
    disableElevation();
    antelevat = angleX;
    elevMoveActive = false;
    return;
  }

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

  if ((millis() - elevMoveStartMs) > kMoveTimeoutMs) {
    Serial.println("Elevation timeout");
    disableElevation();
    elevMoveActive = false;
    return;
  }

  if (elevMoveDirectionUp) {
    if (angleX >= (elevTarget - angfrenadoelev)) {
      disableElevation();
      antelevat = angleX;
      elevMoveActive = false;
    }
  } else {
    if (angleX <= (elevTarget + angfrenadoelev)) {
      disableElevation();
      antelevat = angleX;
      elevMoveActive = false;
    }
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
