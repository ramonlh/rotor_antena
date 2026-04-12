

#include <Arduino.h>
#include "web_pages.h"
#include "app_state.h"
#include "sat_tracking.h"
#include "network_services.h"
#include "rotor_positioning.h"
#include "sat_catalog.h"
#include "sat_radio_info.h"
#include "sat_favorites.h"
#include "sat_search.h"
#include "sat_visibility.h"
#include "sat_visible_now.h"

static String buildAzimuthGauge(float az, float satAz, bool showTarget) {
  while (az < 0.0f) az += 360.0f;
  while (az >= 360.0f) az -= 360.0f;

  while (satAz < 0.0f) satAz += 360.0f;
  while (satAz >= 360.0f) satAz -= 360.0f;

  String s;
  s += "<div class='gauge-card'>";
  s += "<div class='gauge-title'>Azimut</div>";
  s += "<svg viewBox='0 0 200 200' class='gauge-svg'>";
  s += "<circle cx='100' cy='100' r='82' class='dial'/>";

  s += "<line x1='100' y1='18' x2='100' y2='32' class='tick-major'/>";
  s += "<line x1='182' y1='100' x2='168' y2='100' class='tick-major'/>";
  s += "<line x1='100' y1='182' x2='100' y2='168' class='tick-major'/>";
  s += "<line x1='18' y1='100' x2='32' y2='100' class='tick-major'/>";

  s += "<text x='100' y='14' text-anchor='middle'>N</text>";
  s += "<text x='188' y='105' text-anchor='middle'>E</text>";
  s += "<text x='100' y='198' text-anchor='middle'>S</text>";
  s += "<text x='12' y='105' text-anchor='middle'>O</text>";

  // azul = satélite
  if (showTarget) {
    s += "<g transform='rotate(" + String(satAz, 1) + " 100 100)'>";
    s += "<line x1='100' y1='100' x2='100' y2='22' "
         "style='stroke:#1565c0;stroke-width:3;stroke-linecap:round;'/>";
    s += "</g>";
  }

  // roja = antena
  s += "<g transform='rotate(" + String(az, 1) + " 100 100)'>";
  s += "<line x1='100' y1='100' x2='100' y2='42' class='needle'/>";
  s += "<polygon points='100,30 95,44 105,44' class='needle-fill'/>";
  s += "</g>";

  s += "<circle cx='100' cy='100' r='5' class='hub'/>";
  s += "</svg>";
  s += "<div class='gauge-value'>ANT " + String(az, 1) + "&deg; | SAT " + String(satAz, 1) + "&deg;</div>";
  s += "</div>";
  return s;
}

static String buildElevationGauge(float el, float satEl, bool showTarget) {
  // antena: solo 0..90
  if (el < 0.0f) el = 0.0f;
  if (el > 90.0f) el = 90.0f;

  // satélite: permitir elevación real
  float satElDraw = satEl;
  if (satElDraw < -90.0f) satElDraw = -90.0f;
  if (satElDraw >  90.0f) satElDraw =  90.0f;

  const int cx = 100;
  const int cy = 100;
  const int r  = 82;   // mismo tamaño que azimut

  float rot    = -el;
  float satRot = -satElDraw;

  String s;
  s += "<div class='gauge-card'>";
  s += "<div class='gauge-title'>Elevación</div>";
  s += "<svg viewBox='0 0 200 200' class='gauge-svg'>";

  // círculo completo tenue
  s += "<circle cx='100' cy='100' r='82' "
       "style='fill:none;stroke:#d6d6d6;stroke-width:2;'/>";

  // cuadrante útil 0..90 más marcado
  s += "<path d='M 182 100 A 82 82 0 0 0 100 18' class='dial'/>";

  // ejes principales útiles
  s += "<line x1='100' y1='100' x2='182' y2='100' class='horizon'/>";
  s += "<line x1='100' y1='100' x2='100' y2='18' class='tick-major'/>";

  // marcas del cuadrante útil
  s += "<g style='stroke:#777;stroke-width:2;stroke-linecap:round;'>";
  s += "<line x1='174' y1='100' x2='182' y2='100'/>";
  s += "<g transform='rotate(-15 100 100)'><line x1='174' y1='100' x2='182' y2='100'/></g>";
  s += "<g transform='rotate(-30 100 100)'><line x1='170' y1='100' x2='182' y2='100'/></g>";
  s += "<g transform='rotate(-45 100 100)'><line x1='168' y1='100' x2='182' y2='100'/></g>";
  s += "<g transform='rotate(-60 100 100)'><line x1='170' y1='100' x2='182' y2='100'/></g>";
  s += "<g transform='rotate(-75 100 100)'><line x1='174' y1='100' x2='182' y2='100'/></g>";
  s += "</g>";

  // textos útiles
  s += "<text x='188' y='105' text-anchor='middle'>0</text>";
  s += "<text x='160' y='47' text-anchor='middle'>45</text>";
  s += "<text x='100' y='14' text-anchor='middle'>90</text>";

  // azul = satélite, usando elevación real
  if (showTarget) {
    s += "<g transform='rotate(" + String(satRot, 1) + " 100 100)'>";
    s += "<line x1='100' y1='100' x2='182' y2='100' "
         "style='stroke:#1565c0;stroke-width:3;stroke-linecap:round;opacity:0.8;'/>";
    s += "</g>";
  }

  // roja = antena, solo zona útil
  s += "<g transform='rotate(" + String(rot, 1) + " 100 100)'>";
  s += "<line x1='100' y1='100' x2='164' y2='100' class='needle'/>";
  s += "<polygon points='174,100 160,95 160,105' class='needle-fill'/>";
  s += "</g>";

  s += "<circle cx='100' cy='100' r='5' class='hub'/>";
  s += "</svg>";
  s += "<div class='gauge-value'>ANT " + String(el, 1) + "&deg; | SAT " + String(satEl, 1) + "&deg;</div>";
  s += "</div>";
  return s;
}

String buildHtml(bool refrescar) {
  String ptr = "<!DOCTYPE html> <html>\n";

  ptr += "<head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";  ptr += "<title>Control Antena</title>\n";
  ptr += "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr += "body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n";

  ptr += ".button {display:inline-block;min-width:72px;background-color:#3498db;border:none;color:white;padding:8px 12px;text-decoration:none;font-size:17px;margin:4px;cursor:pointer;border-radius:6px;line-height:1.2;}\n";
  ptr += ".toolbar,.movebar,.statusbar,.manualbar{display:flex;flex-wrap:wrap;justify-content:center;align-items:center;gap:6px;margin:6px 0;}\n";
  ptr += ".manualbar input[type=number]{width:58px;padding:4px;margin:0;font-size:14px;}\n";
  ptr += ".toolbar .button,.movebar .button{width:auto;min-width:70px;}\n";
  ptr += "body{margin-top:8px;}\n";

  ptr += "p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  ptr += "input[type=number]{width:70px;padding:6px;margin:4px;font-size:16px;}\n";
  ptr += "input[type=submit]{width:auto;}\n";  
  ptr += ".gauges{display:flex;flex-wrap:wrap;justify-content:center;gap:8px;margin:8px 0 12px;}\n";
  ptr += ".gauge-card{background:#f6f6f6;border:1px solid #ddd;border-radius:10px;padding:6px 8px;min-width:130px;}\n";
  ptr += ".gauge-title{font-size:14px;color:#444;font-weight:bold;margin-bottom:4px;}\n";
  ptr += ".gauge-svg{width:130px;height:auto;}\n";
  ptr += ".dial{fill:none;stroke:#555;stroke-width:4;}\n";
  ptr += ".horizon{stroke:#999;stroke-width:2;}\n";
  ptr += ".tick-major{stroke:#666;stroke-width:3;}\n";
  ptr += ".hub{fill:#333;stroke:#333;}\n";
  ptr += ".needle{stroke:#d33;stroke-width:4;stroke-linecap:round;}\n";
  ptr += ".needle-fill{fill:#d33;}\n";
  ptr += ".gauge-value{font-size:15px;font-weight:bold;color:#222;margin-top:2px;}\n";
  ptr += ".radioinfo{background:#f6f6f6;border:1px solid #ddd;border-radius:10px;padding:8px;margin:8px 0;font-size:14px;line-height:1.35;}\n";
  ptr += ".button-disabled{display:inline-block;min-width:72px;background-color:#999;border:none;color:#eee;padding:8px 12px;text-decoration:none;font-size:17px;margin:4px;cursor:not-allowed;border-radius:6px;line-height:1.2;pointer-events:none;}\n";
  ptr += "button.button{appearance:none;-webkit-appearance:none;}\n";
  ptr += "</style>\n";
  ptr += "</head>\n";
  ptr += "<body>\n";

  stamp.getDateTime(unixtime);
  ptr += "V 1.0 Date-Time: \n";
  ptr += String(stamp.year) + "/" + String(stamp.month) + "/" + String(stamp.day) + " - ";
  ptr += "  " + String(stamp.hour) + ":" + String(stamp.minute) + ":" + String(stamp.second);

  ptr += " - WiFi: <strong>" + getWifiStatusText() + "</strong> | ";
  ptr += "Modo: <strong>";
  ptr += simulationMode ? "SIMULACIÓN" : "REAL";
  ptr += "</strong><br>";

  ptr += "<div id='liveview_top'>";

  if (lastresponse == 200 && tabunixtime[0] != 0) {
    ptr += "Predicción OK";
  } else if (lastresponse == -2) {
    ptr += "Sin predicción N2YO";
  } else if (lastresponse == -3) {
    ptr += "JSON invalido N2YO";
  } else if (lastresponse == -1) {
    ptr += "WiFi no conectada";
  } else {
    ptr += "Fallo prediccion";
  }
  ptr += "<br>";
  //ptr += "DEBUG pred: lastresponse=" + String(lastresponse) + " tab0=" + String(tabunixtime[0]) + "<br>";

  ptr += "<div class='gauges'>";
  bool showSatMarker = (tabunixtime[0] != 0);
  ptr += buildAzimuthGauge(angleZ, satazimut, showSatMarker);
  ptr += buildElevationGauge(angleX, satelevat, showSatMarker);
  ptr += "</div>";
  ptr += "</div>";   // cierre de liveview_top

  ptr += "<div class='statusbar'>";

  ptr += "<form action='/' method='POST' style='margin:0;'>";
  ptr += "<label>Satelite </label>";

  ptr += "<select name='SATS' id='sats'>";
  if (hasSatFavorites()) ptr += buildFavoriteOptionsHtml(satid);
  else ptr += buildSatelliteOptionsHtml(satid);
  ptr += "</select>";

  ptr += " <input type='submit' value='Enviar'>";
  ptr += "</form>";
  ptr += "</div>";

  bool satVisible = isVisibleNow(satid);
  ptr += "Visible ahora: <strong>";
  ptr += isVisibleNow(satid) ? "SI" : "NO";
  ptr += "</strong><br>";

  ptr += "<div class='toolbar'>";
  ptr += "<a class='button button-on' href='/togglesim'>";
  ptr += simulationMode ? "Real" : "Simul";
  ptr += "</a>";
  ptr += "<a class='button button-on' href='/'>Inicio</a>";

  if (enableseguirsat) {
    ptr += "<a class='button button-on' href='/seguirsat'>NoSeg</a>";
  } else if (satVisible) {
    ptr += "<a class='button button-on' href='/seguirsat'>Seguir</a>";
  } else {
    ptr += "<span class='button-disabled'>Seguir</span>";
  }

  ptr += "<a class='button button-on' href='/getdata'>GetP</a>";
  ptr += "<a class='button button-on' href='/config'>Config</a>";
  ptr += "</div>";
  //ptr += "Lista: <strong>" + hasSatFavorites() ? "Favoritos FFat" : "Catalogo base</strong><br>";
  ptr += "<div class='manualbar'>";
  ptr += "<a class='button button-on' href='/iraorigen'>Origen</a>";
  ptr += "<form action='/orientarmanual' method='GET' style='margin:0;'>";
  ptr += "Az <input type='number' name='az' min='0' max='360' step='1' value='" + String(orientMoveActive ? orientTargetAz : angleZ, 1) + "'>";
  ptr += " El <input type='number' name='el' min='0' max='90' step='1' value='" + String(orientMoveActive ? orientTargetEl : angleX, 1) + "'>";  ptr += " <input type='submit' class='button button-on' value='Manual'>";
  ptr += "</form>";
  ptr += "</div>";

  ptr += "<div class='movebar'>";
  ptr += "<button type='button' class='button button-on' onclick=\"sendCmd('/arriba')\">Arriba</button>";
  ptr += "<button type='button' class='button button-on' onclick=\"sendCmd('/abajo')\">Abajo</button>";
  ptr += "<button type='button' class='button button-on' onclick=\"sendCmd('/giroizda')\">Izda</button>";
  ptr += "<button type='button' class='button button-on' onclick=\"sendCmd('/girodcha')\">Dcha</button>";
  ptr += "</div>";

  ptr += "<div id='liveview_bottom'>";
  ptr += "Orientacion: <strong>";
  if (!orientMoveActive) ptr += "IDLE";
  else if (orientPhase == ORIENT_WAIT_ELEV) ptr += "MOV_ELEV";
  else if (orientPhase == ORIENT_WAIT_AZ) ptr += "MOV_AZ";
  else ptr += "???";
  ptr += "</strong>";

  ptr += " - Movimiento: <strong>";
  ptr += getMotionStatusText();
  ptr += "</strong><br>";
  ptr += "</div>";

  ptr += "sat id: " + String(satid) + "  sat name: " + String(satname) + "<br>";

ptr += "Fuente radio: <strong>";
ptr += getRadioInfoSourceText();
ptr += "</strong><br>";
ptr += buildRadioInfoHtml(satid);

ptr += "<script>";
ptr += "async function refreshLiveView(){";
ptr += "  try {";
ptr += "    const r = await fetch('/', {cache:'no-store'});";
ptr += "    const html = await r.text();";
ptr += "    const doc = new DOMParser().parseFromString(html, 'text/html');";

ptr += "    const srcTop = doc.getElementById('liveview_top');";
ptr += "    const dstTop = document.getElementById('liveview_top');";
ptr += "    if (srcTop && dstTop) dstTop.innerHTML = srcTop.innerHTML;";

ptr += "    const srcBottom = doc.getElementById('liveview_bottom');";
ptr += "    const dstBottom = document.getElementById('liveview_bottom');";
ptr += "    if (srcBottom && dstBottom) dstBottom.innerHTML = srcBottom.innerHTML;";

ptr += "  } catch(e) { console.log('refreshLiveView error', e); }";
ptr += "}";

ptr += "function sendCmd(url){";
ptr += "  fetch(url, {method:'GET', cache:'no-store'})";
ptr += "    .then(() => setTimeout(refreshLiveView, 80))";
ptr += "    .catch(err => console.log('sendCmd error', err));";
ptr += "}";

ptr += "setInterval(refreshLiveView, 700);";
ptr += "</script>";

  ptr += "</body>\n</html>\n";
  return ptr;
}

static String formatNextPass(uint32_t ts, float maxEl) {
  if (ts == 0) return "-";

  stamp.getDateTime(ts);

  char buf[40];
  snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d",
           stamp.day, stamp.month, stamp.year, stamp.hour, stamp.minute);
  return String(buf) + " UTC (" + String(maxEl, 1) + "&deg;)";
}

String buildConfigHtml() {
  String ptr = "<!DOCTYPE html><html>\n";
  ptr += "<head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>\n";
  ptr += "<title>Config</title>\n";
  ptr += "<style>";
  ptr += "html{font-family:Helvetica;text-align:center;}";
  ptr += "body{margin:8px;}";
  ptr += ".button{display:inline-block;min-width:64px;background-color:#3498db;border:none;color:white;padding:6px 10px;text-decoration:none;font-size:15px;margin:3px;cursor:pointer;border-radius:6px;line-height:1.2;}";
  ptr += ".topbar,.cfgbar,.formbar{display:flex;flex-wrap:wrap;justify-content:center;align-items:center;gap:6px;margin:6px 0;}";
  ptr += ".panel{background:#f6f6f6;border:1px solid #ddd;border-radius:10px;padding:8px;margin:8px auto;max-width:760px;}";
  ptr += "table{margin:auto;border-collapse:collapse;width:100%;max-width:760px;font-size:14px;}";
  ptr += "th,td{border:1px solid #ccc;padding:5px 8px;}";
  ptr += "th{background:#eeeeee;}";
  ptr += "td:last-child{white-space:nowrap;}";
  ptr += "input[type=number]{width:90px;padding:4px;font-size:14px;}";
  ptr += "input[type=text]{width:140px;padding:4px;font-size:14px;}";
  ptr += "h4{margin:6px 0 8px 0;}";
  ptr += ".small{font-size:13px;color:#555;}";
  ptr += "</style></head><body>\n";

  ptr += "<div class='topbar'>";
  ptr += "<a class='button' href='/'>Volver</a>";
  ptr += "<a class='button' href='/reset'>Reset</a>";
  ptr += "</div>";

  ptr += "<div class='panel'>";
  ptr += "<h4>Configuraci&oacute;n</h4>";
  ptr += "<div class='small'>Favoritos en FFat: " + String(getSatFavoritesCount()) + "</div>";

  ptr += "<div class='cfgbar'>";
  ptr += "<a class='button' href='/restorefavorites'>Restaurar cat&aacute;logo</a>";
  ptr += "<a class='button' href='/reloadfavorites'>Recargar FFat</a>";
  ptr += "</div>";

ptr += "<div class='panel'>";
ptr += "<h4>Visibles ahora</h4>";
ptr += "<a class='button' href='/searchvisible'>Buscar visibles ahora</a>";
ptr += "</div>";
if (getVisibleNowCount() > 0) {
  ptr += "<div class='panel'>";
  ptr += "<table>";
  ptr += "<tr><th>NORAD</th><th>Nombre</th><th>Acci&oacute;n</th></tr>";

  for (int i = 0; i < getVisibleNowCount(); ++i) {
    VisibleSatItem item = getVisibleNowItem(i);

    ptr += "<tr>";
    ptr += "<td>" + String(item.noradId) + "</td>";
    ptr += "<td>" + item.name + "</td>";
    ptr += "<td>";

    ptr += "<form action='/selectvisible' method='GET' style='display:inline;margin:0;'>";
    ptr += "<input type='hidden' name='norad' value='" + String(item.noradId) + "'>";
    ptr += "<input type='hidden' name='name' value='" + item.name + "'>";
    ptr += "<input type='submit' class='button' value='Seleccionar'>";
    ptr += "</form>";

    ptr += "<form action='/addvisiblefavorite' method='GET' style='display:inline;margin:0;'>";
    ptr += "<input type='hidden' name='norad' value='" + String(item.noradId) + "'>";
    ptr += "<input type='hidden' name='name' value='" + item.name + "'>";
    ptr += "<input type='submit' class='button' value='A&ntilde;adir'>";
    ptr += "</form>";

    ptr += "</td>";
    ptr += "</tr>";
  }

  ptr += "</table>";
  ptr += "</div>";
}

  ptr += "<div class='formbar'>";
  ptr += "<form action='/addfavoritecustom' method='GET' style='margin:0;'>";
  ptr += "NORAD <input type='number' name='norad' min='1' step='1'>";
  ptr += " Nombre <input type='text' name='name'>";
  ptr += " <input type='submit' class='button' value='A&ntilde;adir'>";
  ptr += "</form>";
  ptr += "</div>";
  ptr += "</div>";

  ptr += "<table>";
  ptr += "<tr><th>NORAD</th><th>Nombre</th><th>Ahora</th><th>Pr&oacute;xima hora/Zenit</th><th>Acci&oacute;n</th></tr>";

  for (int i = 0; i < getSatFavoritesCount(); ++i) {
    SatFavoriteItem item = getSatFavorite(i);
    ptr += "<tr>";
    ptr += "<td>" + String(item.noradId) + "</td>";
    ptr += "<td>" + item.name + "</td>";

    ptr += "<td>";
    ptr += isFavoriteVisibleNow(item.noradId) ? "VISIBLE" : "NO";
    ptr += "</td>";

    ptr += "<td>";
    if (hasFavoriteFuturePass(item.noradId)) {
    ptr += formatNextPass(getFavoriteNextPassUTC(item.noradId), getFavoriteNextPassMaxEl(item.noradId));
    } else {
      ptr += "-";
    }
    ptr += "</td>";

    ptr += "<td><a class='button' href='/removefavorite?norad=" + String(item.noradId) + "'>Quitar</a></td>";
    ptr += "</tr>";
  }

  ptr += "</table>";
  ptr += "<div class='panel'>";
  ptr += "<h4>Buscar sat&eacute;lite</h4>";
  ptr += "<form action='/searchsat' method='GET' style='margin:0;'>";
  ptr += "Nombre <input type='text' name='q'>";
  ptr += " <input type='submit' class='button' value='Buscar'>";
  ptr += "</form>";
  ptr += "</div>";
  if (getSatSearchResultCount() > 0) {
  ptr += "<div class='panel'>";
  ptr += "<table>";
  ptr += "<tr><th>NORAD</th><th>Nombre</th><th>Acci&oacute;n</th></tr>";

  for (int i = 0; i < getSatSearchResultCount(); ++i) {
    SatSearchItem item = getSatSearchResult(i);
    ptr += "<tr>";
    ptr += "<td>" + String(item.noradId) + "</td>";
    ptr += "<td>" + item.name + "</td>";
    ptr += "<td>";
    ptr += "<form action='/addfavoritefromsearch' method='GET' style='margin:0;'>";
    ptr += "<input type='hidden' name='norad' value='" + String(item.noradId) + "'>";
    ptr += "<input type='hidden' name='name' value='" + item.name + "'>";
    ptr += "<input type='submit' class='button' value='A&ntilde;adir'>";
    ptr += "</form>";
    ptr += "</td>";
    ptr += "</tr>";
  }

  ptr += "</table>";
  ptr += "</div>";
}

  ptr += "</body></html>\n";
  return ptr;
}