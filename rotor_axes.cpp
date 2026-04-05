

#include <Arduino.h>
#include "rotor_axes.h"
#include "rotor_io.h"
#include "imu_sensor.h"
#include "app_state.h"
#include "config.h"

void setElevation(float elev) {
  const unsigned long initTime = millis();
  updateAntennaPosition();
  disableElevation();
  digitalWrite(kElev1pin, ON);
  digitalWrite(kElev2pin, ON);

  if ((elev > -90) && (elev < 90)) {
    enableElevation();
    while (angleX < elev - angfrenadoelev) {
      updateAntennaPosition();
      if ((millis() - initTime) > kMoveTimeoutMs) {
        disableElevation();
        return;
      }
    }
    disableElevation();

    digitalWrite(kElev1pin, OFF);
    digitalWrite(kElev2pin, OFF);
    enableElevation();
    while (angleX > elev + angfrenadoelev) {
      updateAntennaPosition();
      if ((millis() - initTime) > kMoveTimeoutMs) {
        disableElevation();
        return;
      }
    }
    disableElevation();
  }
}