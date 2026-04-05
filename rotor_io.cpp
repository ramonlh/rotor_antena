

#include <Arduino.h>
#include "rotor_io.h"
#include "config.h"

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
  digitalWrite(kElevpin, ON);
}

void disableElevation() {
  digitalWrite(kElevpin, OFF);
  digitalWrite(kElev1pin, OFF);
  digitalWrite(kElev2pin, OFF);
}

void disableRotation() {
  digitalWrite(kGiropin, OFF);
  digitalWrite(kGiro1pin, OFF);
  digitalWrite(kGiro2pin, OFF);
}