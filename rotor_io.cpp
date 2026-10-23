

#include <Arduino.h>
#include "rotor_io.h"
#include "config.h"
#include "app_state.h"

void initRotorPins() {
  pinMode(kElevpin, OUTPUT);
  pinMode(kElev1pin, OUTPUT);
  pinMode(kElev2pin, OUTPUT);
  pinMode(kGiropin, OUTPUT);
  pinMode(kGiro1pin, OUTPUT);
  pinMode(kGiro2pin, OUTPUT);

  digitalWrite(kElevpin, OFF);
  digitalWrite(kElev1pin, OFF);
  digitalWrite(kElev2pin, OFF);
  digitalWrite(kGiropin, OFF);
  digitalWrite(kGiro1pin, OFF);
  digitalWrite(kGiro2pin, OFF);
}

void enableElevation() {
  if (simulationMode) {
    Serial.println("SIM: enableElevation");
    return;
    }  
  digitalWrite(kElevpin, ON);
}

void disableElevation() {
  // Apagar los relés siempre, también en simulación: si se cambia de modo
  // con un movimiento real en curso, el motor no debe quedar activado.
  digitalWrite(kElevpin, OFF);
  digitalWrite(kElev1pin, OFF);
  digitalWrite(kElev2pin, OFF);
}

void setElevationDirectionUp() {
  // En este rotor, ambos relés de sentido activados hacen subir.
  digitalWrite(kElev1pin, ON);
  digitalWrite(kElev2pin, ON);
}

void setElevationDirectionDown() {
  // En este rotor, ambos relés de sentido desactivados hacen bajar.
  digitalWrite(kElev1pin, OFF);
  digitalWrite(kElev2pin, OFF);
}

void disableRotation() {
  // Igual que disableElevation(): apagar siempre los relés.
  digitalWrite(kGiropin, OFF);
  digitalWrite(kGiro1pin, OFF);
  digitalWrite(kGiro2pin, OFF);
}
