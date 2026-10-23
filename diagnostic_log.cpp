#include <Arduino.h>
#include "diagnostic_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static constexpr int kEventCount = 6;
struct DiagnosticEvent {
  uint32_t uptimeSec;
  char text[72];
};

static DiagnosticEvent gEvents[kEventCount];
static int gNextEvent = 0;
static int gStoredEvents = 0;
static portMUX_TYPE gEventMux = portMUX_INITIALIZER_UNLOCKED;

void addDiagnosticEvent(const char* text) {
  if (text == nullptr || text[0] == '\0') return;
  portENTER_CRITICAL(&gEventMux);
  gEvents[gNextEvent].uptimeSec = millis() / 1000UL;
  strlcpy(gEvents[gNextEvent].text, text, sizeof(gEvents[gNextEvent].text));
  gNextEvent = (gNextEvent + 1) % kEventCount;
  if (gStoredEvents < kEventCount) ++gStoredEvents;
  portEXIT_CRITICAL(&gEventMux);
}

int getDiagnosticEventCount() {
  portENTER_CRITICAL(&gEventMux);
  const int count = gStoredEvents;
  portEXIT_CRITICAL(&gEventMux);
  return count;
}

bool getDiagnosticEvent(int newestIndex, uint32_t& uptimeSec, char* text, size_t textSize) {
  if (text == nullptr || textSize == 0) return false;
  portENTER_CRITICAL(&gEventMux);
  if (newestIndex < 0 || newestIndex >= gStoredEvents) {
    portEXIT_CRITICAL(&gEventMux);
    return false;
  }
  int index = gNextEvent - 1 - newestIndex;
  while (index < 0) index += kEventCount;
  uptimeSec = gEvents[index].uptimeSec;
  strlcpy(text, gEvents[index].text, textSize);
  portEXIT_CRITICAL(&gEventMux);
  return true;
}
