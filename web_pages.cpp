

#include <Arduino.h>
#include "web_pages.h"
#include "app_state.h"
#include "satellite_tracker.h"

String buildHtml(bool refrescar) {
  if (server.hasArg("SATS")) {
    if (server.arg("SATS").toInt() > 0) {
      satid = server.arg("SATS").toInt();
    }
    getPositions();
  }

  String ptr = "<!DOCTYPE html> <html>\n";
  if (refrescar) ptr += "<META http-equiv=refresh content=10>";

  ptr += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr += "<title>Control Antena</title>\n";
  ptr += "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr += "body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n";
  ptr += ".button {display: inline-block;width: 80px;background-color: #3498db;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n";
  ptr += ".button-on {background-color: #3498db;}\n";
  ptr += ".button-on:active {background-color: #2980b9;}\n";
  ptr += ".button-off {background-color: #34495e;}\n";
  ptr += ".button-off:active {background-color: #2c3e50;}\n";
  ptr += "p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  ptr += "</style>\n";
  ptr += "</head>\n";
  ptr += "<body>\n";

  stamp.getDateTime(unixtime);
  ptr += "V 1.0 UnixTime - Date - Time: " + String(unixtime)+ " - "  + "\n";
  ptr += String(stamp.year) + "/" + String(stamp.month) + "/" + String(stamp.day) + " - ";
  ptr += "  " + String(stamp.hour) + ":" + String(stamp.minute) + ":" + String(stamp.second);
  ptr += "<br><br>";
  ptr += "ANTENA  Az/El: <strong>" + String(angleZ) + " / " + String(angleX) + "</strong><br>";
  ptr += "ISS Az/El: <strong>" + String(satazimut) + " / " + String(satelevat) + "</strong> (";
  ptr += (lastresponse == 200) ? "Ok" : "Error";
  ptr += ")<br><br>";

  ptr += "<form action='/' method='POST'>";
  ptr += "<label>Satelite </label>\n";
  ptr += "<select name=SATS id=sats>";
  ptr += String("<option value=25544") + (satid == 25544 ? " selected" : "") + ">ISS</option>";
  ptr += String("<option value=48274") + (satid == 48274 ? " selected" : "") + ">Tiangong</option></select>";
  ptr += "\n<input type='submit' name='SUBMIT' value='Enviar'></form><br>";

  ptr += "<a class=\"button button-on\" href=\"/\">Posicion</a>\n";
  ptr += "<a class=\"button button-on\" href=\"/iraorigen\">N-0</a><br>";
  ptr += "<a class=\"button button-on\" href=\"/orientarmanual\">Manual</a>\n";
  ptr += "<a class=\"button button-on\" href=\"/seguirsat\">";
  if (enableseguirsat) ptr += "NO ";
  ptr += "Seguir</a><br>";
  ptr += "<a class=\"button button-on\" href=\"/arriba\">Arriba</a>\n";
  ptr += "<a class=\"button button-on\" href=\"/abajo\">Abajo</a><br>";
  ptr += "<a class=\"button button-on\" href=\"/giroizda\">Izda</a>\n";
  ptr += "<a class=\"button button-on\" href=\"/girodcha\">Dcha</a><br>";
  ptr += "<a class=\"button button-on\" href=\"/reset\">Reset</a>\n";
  ptr += "<a class=\"button button-on\" href=\"/getdata\">Get P</a><br>";

  ptr += "sat id: " + String(satid) + "  sat name: " + String(satname) + "<br>";
  ptr += "sat tle: " + String(sattle) + "<br>";

  if (satid == 25544) {
    ptr += "FM Bajada 145.800  Subida 145.200<br>";
    ptr += "Rep FM Bajada 145.800  Subida 437.800<br>";
    ptr += "Rep FM tono Bajada 437.800  Subida 145.990 67Hz<br>";
    ptr += "AX25 Bajada 145.825  Subida 145.825 67Hz<br>";
    ptr += "AX25 Bajada 145.825  Subida 145.825 67Hz<br>";
    ptr += "Soyuz Bajada 130.167 AM<br>";
    ptr += "Zarya Bajada 143.625 FM<br>";
    ptr += "Moscu Bajada 166.000 AM<br>";
  }

  ptr += "</body>\n</html>\n";
  return ptr;
}