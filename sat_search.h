#pragma once
#include <Arduino.h>
#include "fixed_text.h"

struct SatSearchItem {
  long noradId;
  FixedText<32> name;
};

struct AmateurCatalogItem {
  long noradId;
  FixedText<32> name;
};

struct SatRadioInspection {
  bool checked;
  bool queryOk;
  long noradId;
  FixedText<32> name;

  int txTotal;
  int txActive;
  int txAmateur;
  int txActiveAmateur;
  int txTwoWay;

  FixedText<128> modes;
  FixedText<192> downlinks;
  FixedText<192> uplinks;
  FixedText<96> types;
  FixedText<224> functions;
  FixedText<96> error;
};

// Búsqueda general CelesTrak por nombre.
bool searchSatellitesByName(const String& query);
int getSatSearchResultCount();
SatSearchItem getSatSearchResult(int index);
void clearSatSearchResults();

// Catálogo CelesTrak GROUP=AMATEUR.
// Se procesa en streaming para no guardar todo el CSV en RAM.
bool loadAmateurRadioCatalog();
bool refreshAmateurRadioCatalog();
int getAmateurCatalogCount();
AmateurCatalogItem getAmateurCatalogItem(int index);
bool hasAmateurCatalog();
const char* getAmateurCatalogStatusText();

// Comprobación individual en SatNOGS.
// No intenta analizar los ~100 satélites de golpe.
bool inspectSatelliteRadio(long noradId, const String& name);
SatRadioInspection getSatRadioInspection();
void clearSatRadioInspection();
