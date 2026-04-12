

#include "sat_catalog.h"

const SatCatalogItem kSatCatalog[] = {
  {25544, "ISS"},
  {48274, "Tiangong"},
  {7530,  "AO-7"},
  {22825, "FO-29"},
  {27607, "SO-50"},
  {39444, "AO-73"},
  {40069, "XW-2A"},
  {40070, "XW-2B"},
  {40071, "XW-2C"},
  {40072, "XW-2D"},
  {40074, "XW-2F"},
  {40931, "LilacSat-2"},
  {43017, "AO-91"},
  {51074, "TEVEL-1"}
};

const size_t kSatCatalogCount = sizeof(kSatCatalog) / sizeof(kSatCatalog[0]);

String buildSatelliteOptionsHtml(long selectedSatId) {
  String out;
  for (size_t i = 0; i < kSatCatalogCount; ++i) {
    out += "<option value='";
    out += String(kSatCatalog[i].noradId);
    out += "'";
    if (kSatCatalog[i].noradId == selectedSatId) out += " selected";
    out += ">";
    out += kSatCatalog[i].name;
    out += "</option>";
  }
  return out;
}

const char* findSatelliteNameByNorad(long noradId) {
  for (size_t i = 0; i < kSatCatalogCount; ++i) {
    if (kSatCatalog[i].noradId == noradId) return kSatCatalog[i].name;
  }
  return "Unknown";
}