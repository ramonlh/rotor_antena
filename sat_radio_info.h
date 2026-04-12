

#pragma once
#include <Arduino.h>

String buildRadioInfoHtml(long noradId);
bool refreshRadioInfoForSat(long noradId);

const char* getRadioInfoSourceText();
int getLastRadioHttpCode();