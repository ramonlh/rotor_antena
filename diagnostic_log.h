#pragma once
#include <Arduino.h>

void addDiagnosticEvent(const char* text);
int getDiagnosticEventCount();
bool getDiagnosticEvent(int newestIndex, uint32_t& uptimeSec, char* text, size_t textSize);
