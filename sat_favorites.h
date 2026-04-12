#pragma once
#include <Arduino.h>

struct SatFavoriteItem {
  long noradId;
  String name;
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