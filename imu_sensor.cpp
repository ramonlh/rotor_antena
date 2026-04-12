#include <Arduino.h>
#include "JY901_Serial.h"
#include "imu_sensor.h"
#include "app_state.h"
#include "config.h"
extern bool simulationMode;

void initImu() {
  mySerial.begin(9600, SERIAL_8N1, kJY901RxPin, kJY901TxPin);
  JY901.attach(mySerial);
}

void updateAntennaPosition() {
  if (simulationMode) {
    return;
    }
  JY901.receiveSerialData();
  angleX = JY901.getRoll();
  angleY = JY901.getPitch();
  angleZ = JY901.getYaw();

  if (angleZ <= 0) {
    angleZ = -angleZ;
  } else {
    angleZ = 360 - angleZ;
  }
}
