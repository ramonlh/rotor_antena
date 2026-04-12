#pragma once
#include <Arduino.h>

struct SatSearchItem {
  long noradId;
  String name;
};

bool searchSatellitesByName(const String& query);
int getSatSearchResultCount();
SatSearchItem getSatSearchResult(int index);
void clearSatSearchResults();