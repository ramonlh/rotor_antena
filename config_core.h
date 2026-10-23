

#pragma once
#include <Arduino.h>

#define OFF LOW
#define ON HIGH

struct SatData {
  long satid;
  String satname;
};

constexpr int kWebServerPort = 91;
constexpr int kTimeZoneHours = 2;
// N2YO devuelve objetos grandes por cada segundo. Treinta posiciones caben
// en el buffer HTTP sin reservar memoria excesiva para el ESP32.
constexpr int kTableSize = 30;
constexpr unsigned long kTickMs = 1000;
