

#include "state_satellite.h"

SatData satData[2] = {{25544, "ISS"}, {48274, "Tiangong"}};

float satazimut = 0.0f;
float satelevat = 0.0f;

uint32_t tabunixtime[kTableSize] = {0};
float tabazimut[kTableSize] = {0.0f};
float tabelevat[kTableSize] = {0.0f};

int lastresponse = 0;
uint32_t unixtime = 0;
long satid = 25544;
String satname = "";
String sattle = "";