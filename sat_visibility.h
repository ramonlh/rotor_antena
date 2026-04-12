#pragma once
#include <Arduino.h>

struct SatVisibilityInfo {
  long noradId;
  bool visibleNow;
  bool hasFuturePass;
  uint32_t nextPassStartUTC;
  float nextPassMaxEl;
};

bool refreshVisibleNow();
bool refreshFutureVisibility(uint8_t daysAhead = 1, uint8_t minElevation = 10);

bool isFavoriteVisibleNow(long noradId);
bool hasFavoriteFuturePass(long noradId);
uint32_t getFavoriteNextPassUTC(long noradId);
float getFavoriteNextPassMaxEl(long noradId);
void clearVisibilityCache();