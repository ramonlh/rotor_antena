

#include <Arduino.h>
#include "rotor_positioning.h"
#include "rotor_axes.h"
#include "rotor_io.h"
#include "imu_sensor.h"
#include "app_state.h"
#include "config.h"

static void rotateRight(float target, byte phase2) {
  const unsigned long initTime = millis();
  disableRotation();
  digitalWrite(kGiro1pin, ON);
  digitalWrite(kGiro2pin, ON);
  digitalWrite(kGiropin, ON);

  if (phase2 == 0) {
    while (angleZ < target - angfrenadoazim) {
      updateAntennaPosition();
      if ((millis() - initTime) > kMoveTimeoutMs) {
        disableRotation();
        return;
      }
    }
  } else {
    while (angleZ < target) {
      updateAntennaPosition();
      if ((millis() - initTime) > kMoveTimeoutMs) {
        disableRotation();
        return;
      }
    }
  }
  disableRotation();
}

static void rotateLeft(float target, byte phase2) {
  const unsigned long initTime = millis();
  disableRotation();
  digitalWrite(kGiro1pin, OFF);
  digitalWrite(kGiro2pin, OFF);
  digitalWrite(kGiropin, ON);

  if (phase2 == 0) {
    while (angleZ > target + angfrenadoazim) {
      updateAntennaPosition();
      if ((millis() - initTime) > kMoveTimeoutMs) {
        disableRotation();
        return;
      }
    }
  } else {
    while (angleZ > target) {
      updateAntennaPosition();
      if ((millis() - initTime) > kMoveTimeoutMs) {
        disableRotation();
        return;
      }
    }
  }
  disableRotation();
}

void setAzimuth(float azim) {
  updateAntennaPosition();

  if (angleZ > 180) {
    if (azim > 180) {
      if (angleZ > azim) {
        rotateLeft(azim, 0);
      } else {
        rotateRight(azim, 0);
      }
    } else {
      if (angleZ > azim) {
        rotateRight(360, 0);
        angleZ = offazimut + 5;
        delay(1000);
        rotateRight(azim, 1);
      }
    }
  } else {
    if (azim <= 180) {
      if (angleZ > azim) {
        rotateLeft(azim, 0);
      } else {
        rotateRight(azim, 0);
      }
    } else {
      if (angleZ < azim) {
        rotateLeft(0, 0);
        angleZ = 360 - offazimut - 5;
        delay(1000);
        rotateLeft(azim, 1);
      }
    }
  }
}

void orientAntenna(float azim, float elev) {
  setElevation(elev);
  setAzimuth(azim);
  delay(1000);
  updateAntennaPosition();
  antazimut = angleZ;
  antelevat = angleX;
}