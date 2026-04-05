

#pragma once
#include <Arduino.h>
#include "config.h"

extern SatData satData[2];

extern float satazimut;
extern float satelevat;

extern uint32_t tabunixtime[kTableSize];
extern float tabazimut[kTableSize];
extern float tabelevat[kTableSize];

extern long satid;
extern String satname;
extern String sattle;
extern int lastresponse;
extern uint32_t unixtime;