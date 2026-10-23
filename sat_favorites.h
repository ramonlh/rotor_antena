#pragma once
#include <Arduino.h>
#include "fixed_text.h"

struct SatFavoriteItem {
  long noradId;
  FixedText<32> name;
};

bool initSatFavorites();
bool loadSatFavorites();
bool saveSatFavorites();

int getSatFavoritesCount();
SatFavoriteItem getSatFavorite(int index);

bool addSatFavorite(long noradId, const String& name);
bool removeSatFavorite(long noradId);
bool hasSatFavorites();

String buildFavoriteOptionsHtml(long selectedSatId);
