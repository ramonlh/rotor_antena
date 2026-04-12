

#include "web_routes.h"
#include "app_state.h"
#include "web_handlers.h"

void initWebRoutes() {
  server.on("/", handleIndex);
  server.on("/index.html", handleIndex);
  server.on("/seguirsat", handleSeguirSat);
  server.on("/iraorigen", handleIrAOrigen);
  server.on("/orientarmanual", handleOrientarManual);
  server.on("/arriba", handleArriba);
  server.on("/abajo", handleAbajo);
  server.on("/giroizda", handleGiroIzda);
  server.on("/girodcha", handleGiroDcha);
  server.on("/getdata", handleGetData);
  server.on("/reset", handleReset);
  server.on("/togglesim", handleToggleSim);
  server.onNotFound(handleNotFound);
  server.on("/addfavorite", handleAddFavorite);
  server.on("/removefavorite", handleRemoveFavorite);
  server.on("/addfavoritecustom", handleAddFavoriteCustom);
  server.on("/config", handleConfigPage);
  server.on("/restorefavorites", handleRestoreFavorites);
  server.on("/config", handleConfigPage);
  server.on("/reloadfavorites", handleReloadFavorites);
  server.on("/searchsat", handleSearchSat);
  server.on("/addfavoritefromsearch", handleAddFavoriteFromSearch);
  server.on("/searchvisible", handleSearchVisibleNow);
  server.on("/selectvisible", handleSelectVisibleSat);
  server.on("/addvisiblefavorite", handleAddVisibleSatToFavorites);
}