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
constexpr int kTableSize = 120;
constexpr unsigned long kTickMs = 1000;
