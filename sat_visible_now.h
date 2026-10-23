#pragma once
#include <Arduino.h>
#include "fixed_text.h"

struct VisibleSatItem {
  long noradId;
  FixedText<32> name;
};

enum SatVisibleState : uint8_t {
  SAT_VIS_UNKNOWN = 0,
  SAT_VIS_NOT_VISIBLE = 1,
  SAT_VIS_VISIBLE = 2
};

// Consulta síncrona. Se conserva por compatibilidad.
bool searchVisibleNow();
bool refreshVisibleNowIfStale(uint32_t maxAgeMs);

// Actualización no bloqueante.
bool requestVisibleNowRefreshIfStale(uint32_t maxAgeMs);
bool isVisibleNowRefreshInProgress();

// Respaldo cuando /above no produce una instantanea: comprueba los favoritos
// uno a uno mediante /positions, sin bloquear el servidor web.
bool requestFavoriteVisibilityRefreshIfStale();
bool isFavoriteVisibilityRefreshInProgress();
long getFavoriteVisibilityCurrentId();
long getFavoriteVisibilityLastId();
SatVisibleState getFavoriteVisibilityLastState();
uint32_t getFavoriteVisibilityRetryRemainingMs();
uint32_t getVisibleNowRetryRemainingMs();
String getVisibleNowLastError();
String getFavoriteVisibilityLastError();

// Servicio autónomo: mantiene la caché actualizada aunque no haya navegador.
bool startVisibleNowAutoRefresh();
void serviceVisibleNowAutoRefresh();

int getVisibleNowCount();
VisibleSatItem getVisibleNowItem(int index);
void clearVisibleNow();

bool hasVisibleNowSnapshot();
uint32_t getVisibleNowAgeMs();

SatVisibleState getVisibleNowState(long noradId);

// Compatibilidad con el código anterior.
bool isVisibleNow(long noradId);
