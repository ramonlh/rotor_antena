

#pragma once
#include <Arduino.h>

struct SatCatalogItem {
  long noradId;
  const char* name;
};

extern const SatCatalogItem kSatCatalog[];
extern const size_t kSatCatalogCount;

String buildSatelliteOptionsHtml(long selectedSatId);
const char* findSatelliteNameByNorad(long noradId);
