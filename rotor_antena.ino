/*
* Servidor web con ESP32
* Librerias necesarias para conectarnos a un entorno Wifi y poder configurar  
* un servidor WEB
*/
#include <WiFi.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "Arduino_JSON.h"

#include <Wire.h>
#include "JY901_Serial.h"
#include "HardwareSerial.h"
#include <UnixTime.h>

#define OFF LOW
#define ON HIGH

typedef struct { 
  long satid;
  String satname;
} satdatatype;

satdatatype satdata[2] = {{25544,"ISS"},{48274,"Tiangong"}};

HardwareSerial mySerial(1);
const int myRx = 4;
const int myTx = 26;

/* Añade tu SSID & Clave para acceder a tu Wifi */
//const char* ssid = "Wifi-Hall_EXT";  // Tu SSID
const char* ssid = "MOVISTAR_8453";  // Tu SSID
const char* password = "18921892";  //Tu Clave
IPAddress local_IP(192, 168, 1, 91);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);    //optional
IPAddress secondaryDNS(8, 8, 4, 4);  //optional

const char *ssidAP = "rotorantena";
const char *passwordAP = "12341234";

WebServer server(91);

uint8_t Giropin = 18;     // relé 7
uint8_t Giro1pin = 17;    // relé 1
uint8_t Giro2pin = 23;    // relé 2
uint8_t Elevpin = 5;      // relé 6
uint8_t Elev1pin = 25;    // relé 3
uint8_t Elev2pin = 27;    // relé 4

int timesubir = 0;
int timebajar = 0;
int timegirarizda = 0;
int timegirardcha = 0;
int inisubir;
int inibajar;
int inigirarizda;
int inigirardcha;
bool subiendo = false;
bool bajando = false;
bool girandoizda = false;
bool girandodcha = false;

float angleX = 0.0;
float angleY = 0.0;
float angleZ = 0.0;

float satazimut = 0.0;
float satelevat = 0.0;
float antazimut = 0.0;
float antelevat = 0.0;
float offazimut = 1.0;
float offelevat = 1.0;
float angfrenadoazim = 2.0;
float angfrenadoelev = 2.0;

const int sizeoftab = 120;
uint32_t tabunixtime[sizeoftab];
float tabazimut[sizeoftab];
float tabelevat[sizeoftab];
bool enableseguirsat = false;
int lastresponse = 0;

UnixTime stamp(2);  // especificar GMT (3 para Moscú)
uint32_t unixtime = 0;

String serverSatName = "https://dweet.io/get/latest/dweet/for/sat";

// tle
String serverPos = "https://api.n2yo.com/rest/v1/satellite/positions/";
String serverPosmydata = "/40.773192631067/-4.2217211826047265/1250/";
String serverPosKey = "/&apiKey=37MN55-ULPQ76-MA346R-5AXY";

unsigned long lastTime = 0;
int transactionid = 0;
long satid = 25544;   // valor por defecto ISS
String satname = "";
String sattle = "";

long timedelay1 = 1000;
long timedelay120 = sizeoftab * 1000;
long time1 = 0;
long timegetpositions = 0;

void enableelevacion()
{
  digitalWrite(Elevpin, ON); 
}

void disableelevacion()
{
  digitalWrite(Elevpin, OFF); 
  digitalWrite(Elev1pin, OFF); 
  digitalWrite(Elev2pin, OFF); 
}

void disablegiro()
{
  digitalWrite(Giropin, OFF);  
  digitalWrite(Giro1pin, OFF);  
  digitalWrite(Giro2pin, OFF);  
}

void handle_OnConnect() {
  server.send(200, "text/html", SendHTML(true)); 
}

void handle_index() {
  getposantena();
  server.send(200, "text/html", SendHTML(true));
}

void getposantena()
{
  JY901.receiveSerialData();
  angleX = JY901.getRoll();
  angleY = JY901.getPitch();
  angleZ = JY901.getYaw();
  if (angleZ <= 0)
    angleZ = -angleZ;
  else
    angleZ = 360 - angleZ;
}

void setelevacion(float elev)
{
  const long maxtime = 30000;   // por si falla, a los 30 segundos para
  long inittime = millis();
  getposantena();
  disableelevacion();
  digitalWrite(Elev1pin, ON);  digitalWrite(Elev2pin, ON);    // subir
  if ((elev > -90) && (elev < 90))
    {
    enableelevacion();
    subiendo = true;
    while (angleX < elev - angfrenadoelev) 
      { 
      getposantena(); 
      if ((millis() - inittime) > maxtime) { disableelevacion(); return; };
      }
    disableelevacion();

    digitalWrite(Elev1pin, OFF);  digitalWrite(Elev2pin, OFF);    // bajar
    enableelevacion();
    bajando = true;
    while (angleX > elev + angfrenadoelev) 
      {
      getposantena(); 
      if ((millis() - inittime) > maxtime) { disableelevacion(); return; };
      }
    disableelevacion();
    }
}


void giroderecha(float B, byte fase2)
{
  Serial.print("giroderecha de-->a  "); Serial.print(angleZ);Serial.print("-->"); Serial.println(B);
  const long maxtime = 30000;   // por si falla, a los 30 segundos para
  long inittime = millis();
  disablegiro();
  digitalWrite(Giro1pin, ON);  digitalWrite(Giro2pin, ON);
  digitalWrite(Giropin, ON);  
  girandodcha = true;
  Serial.print("angleZ 0/1:"); Serial.print(angleZ); Serial.print("/");Serial.println(B);
  if (fase2 == 0)
    {
    while (angleZ < B - angfrenadoazim)
      { 
      getposantena(); 
      if ((millis() - inittime) > maxtime) { disablegiro(); return; };
      }
    }
  else
    {
    while (angleZ < B)
      { 
      getposantena(); 
      if ((millis() - inittime) > maxtime) { disablegiro(); return; };
      }
    }
  disablegiro();
}

void giroizquierda(float B, byte fase2)
{
  const long maxtime = 30000;   // por si falla, a los 30 segundos para
  long inittime = millis();
  disablegiro();
  digitalWrite(Giro1pin, OFF);  digitalWrite(Giro2pin, OFF);
  digitalWrite(Giropin, ON);  
  girandoizda = true;
  if (fase2 == 0)
    {
    while (angleZ > B +  angfrenadoazim)
      { 
      getposantena(); 
      if ((millis() - inittime) > maxtime) { disablegiro(); return; };
      }
    }
  else
    {
    while (angleZ > B)
      { 
      getposantena(); 
      if ((millis() - inittime) > maxtime) { disablegiro(); return; };
      }
    }
  disablegiro();
}

void setazimut(float azim)
{
  const long maxtime = 30000;   // por si falla, a los 30 segundos para
  long inittime = millis();
  getposantena();
  if (angleZ > 180)   // angleZ IZDA
    {
    if (azim > 180)   // Azim IZDA
      {
      if ((angleZ > azim))  // A>B
        {
        giroizquierda(azim, 0);
        }
      else                  // A<B
        {
        giroderecha(azim, 0);
        }
      }
    else              // Azim DCHA
      {
      if ((angleZ > azim))  // A>B
        {
        giroderecha(360, 0);
        Serial.print(" fase2 girando desde/hacia:"); Serial.print(angleZ); Serial.print("/"); Serial.println(azim);
        angleZ = offazimut + 5;
        delay(1000);
        giroderecha(azim, 1);
        }
      else                  // A<B
        {
        // este caso no se da
        }
      }
    }
  else    // angleZ DCHA
    {
    if (azim <= 180)   // Azim DCHA
      {
      if (angleZ > azim)  // A>B
        {
        giroizquierda(azim, 0);
        }
      else                  // A<B
        {
        giroderecha(azim, 0);
        }
      }
    else              // Azim IZDA
      {
      if (angleZ > azim)  // A>B
        {
        // este caso no se da
        }
      else                  // A<B
        {
        Serial.print("girando desde/hacia:"); Serial.print(angleZ); Serial.print("/");Serial.println(0);
        giroizquierda(0, 0);
        angleZ = 360 - offazimut - 5;
        Serial.print("girando desde/hacia:"); Serial.print(angleZ); Serial.print("/");Serial.println(azim);
        delay(1000);
        giroizquierda(azim, 1);
        }
      }
    }
} 

void orientarantena(float azim, float elev)
{
  setelevacion(elev);
  setazimut(azim);
  delay(1000);
  getposantena();
  antazimut = angleZ;
  antelevat = angleX;
}

void handle_seguirsat() {
  enableseguirsat = !enableseguirsat;
  server.send(200, "text/html", SendHTML(false));
}

void handle_iraorigen() {
  orientarantena(0.0, 0.0);
  server.send(200, "text/html", SendHTML(false));
}

void handle_orientarmanual() {
  orientarantena(satazimut, satelevat);
  server.send(200, "text/html", SendHTML(false));
}

void handle_arriba() {
  //if (angleX < 85)
    {
    disableelevacion();
    digitalWrite(Elev1pin, ON);  digitalWrite(Elev2pin, ON);
    enableelevacion();
    delay(500);
    disableelevacion();
    delay(500);;
    getposantena();
    }
  server.send(200, "text/html", SendHTML(false));
}

void handle_abajo() {
  //if (angleX > -85)
    {
    disableelevacion();
    digitalWrite(Elev1pin, OFF);  digitalWrite(Elev2pin, OFF);
    enableelevacion();
    delay(500);
    disableelevacion();
    delay(500);;
    getposantena();
    }
  server.send(200, "text/html", SendHTML(false));
}

void handle_giroizda() {
  disablegiro();
  digitalWrite(Giro1pin, OFF);  digitalWrite(Giro2pin, OFF);
  digitalWrite(Giropin, ON);  
  delay(2000);
  disablegiro();
  delay(500);;
  getposantena();
  server.send(200, "text/html", SendHTML(false));
}

void handle_girodcha() {
  disablegiro();
  digitalWrite(Giro1pin, ON);  digitalWrite(Giro2pin, ON);
  digitalWrite(Giropin, ON);  
  delay(2000);
  digitalWrite(Giropin, OFF);  
  delay(500);;
  getposantena();
  server.send(200, "text/html", SendHTML(false));
}

void handle_reset() {
  ESP.restart();
}

void handle_getdata() {
  getpositions();
  server.send(200, "text/html", SendHTML(false));
}

void handle_NotFound() {
  server.send(404, "text/plain", "La pagina no existe");
}

String SendHTML(bool refrescar) {
  if (server.hasArg("SATS")) {
    if (server.arg("SATS").toInt() > 0)
      satid = server.arg("SATS").toInt();
    getpositions();
  }
  // Cabecera de todas las paginas WEB
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
  ptr += String(stamp.year) + "/" + String(stamp.month) + "/" + String(stamp.day) + " - " ;
  ptr += "  " + String(stamp.hour) + ":" + String(stamp.minute) + ":" + String(stamp.second);
  ptr += "<br><br>";
  ptr += "ANTENA  Az/El: <strong>" + String(angleZ) + " / " + String(angleX) + "</strong><br>";
  ptr += "ISS Az/El: <strong>" + String(satazimut) + " / " + String(satelevat) + "</strong> (";
  if (lastresponse=200)
    ptr += "Ok";
  else
    ptr += "Error";
  ptr += ")<br><br>";

  ptr += "<form action='/' method='POST'>";
  ptr += "<label>Satelite </label>\n";
  ptr += "<select name=SATS id=sats\">";
  ptr = ptr + "<option value=25544"  + (satid==25544?" selected":"") + ">ISS</option>";
  ptr = ptr + "<option value=48274" + (satid==48274?" selected":"") + ">Tiangong</option></select>";
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
  // datos ISS
  if (satid == 25544)  // ISS
    {
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

void cosascadasegundo()
{
  unixtime++;
  getposantena();
  time1 = millis();
  if (unixtime-tabunixtime[0] < 200)
    {
    satazimut = tabazimut[unixtime-tabunixtime[0]];
    satelevat = tabelevat[unixtime-tabunixtime[0]];
    }
  if (enableseguirsat)
    {
    if ((abs(antazimut - satazimut) > offazimut) || (abs(antelevat - satelevat) > offelevat))
      {
      orientarantena(satazimut, satelevat);
      }
    }
}

void getpositions()
{
  timegetpositions = millis();
  if(WiFi.status()== WL_CONNECTED){
    HTTPClient http;
    String serverPath = serverPos + String(satid) + serverPosmydata + String(sizeoftab) + serverPosKey;
    Serial.println(serverPath);
    http.begin(serverPath.c_str());
    int httpResponseCode = http.GET();
    lastresponse = httpResponseCode;
    if (httpResponseCode > 0) {
      String payload = http.getString();
      JSONVar myObject = JSON.parse(payload);
      //satid = myObject["info"]["satid"];
      satname = String(myObject["info"]["satname"]);
      for (int i=0; i<myObject["positions"].length(); i++)
        {
        tabunixtime[i] = uint32_t(myObject["positions"][i]["timestamp"]);
        tabazimut[i] = double(myObject["positions"][i]["azimuth"]);
        tabelevat[i] = double(myObject["positions"][i]["elevation"]);
        }
      unixtime = tabunixtime[0];
      }
    else 
      {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
      }
    http.end();
  }
}

void setup() {
  Serial.begin(115200);
  mySerial.begin(9600, SERIAL_8N1, myRx, myTx);
  JY901.attach(mySerial);
  // setBaudRate(2);   // 2=9600,  6=115200 hay que reiniciar

  pinMode(Elevpin, OUTPUT);
  digitalWrite(Elevpin, OFF); 
  pinMode(Elev1pin, OUTPUT);
  pinMode(Elev2pin, OUTPUT);
  pinMode(Giropin, OUTPUT);
  digitalWrite(Giropin, OFF); 
  pinMode(Giro1pin, OUTPUT);
  pinMode(Giro2pin, OUTPUT);
  digitalWrite(Elev1pin, OFF); digitalWrite(Elev2pin, OFF);
  digitalWrite(Giro1pin, OFF); digitalWrite(Giro2pin, OFF);

  WiFi.mode(WIFI_STA);
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA Failed to configure");
  }
  WiFi.begin(ssid, password);
  Serial.println("");

  int intentos=0;
  while ((WiFi.status() != WL_CONNECTED) && (intentos<30))
    {
    delay(500);
    Serial.print(".");
    intentos++;
    }
  if (WiFi.status() == WL_CONNECTED)
    {
    Serial.println("");
    Serial.print("Conectado a ");
    Serial.println(ssid);
    Serial.print("Direccion IP: ");
    Serial.println(WiFi.localIP());
    }
else
  {
  if (!WiFi.softAP(ssidAP, passwordAP)) {
    log_e("Soft AP creation failed.");
    while (1);
    }
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  }

  server.on("/", handle_index); 
  server.on("/index.html", handle_index); 
  server.on("/seguirsat", handle_seguirsat); 
  server.on("/iraorigen", handle_iraorigen); 
  server.on("/orientarmanual", handle_orientarmanual); 
  server.on("/arriba", handle_arriba); 
  server.on("/abajo", handle_abajo); 
  server.on("/giroizda", handle_giroizda); 
  server.on("/girodcha", handle_girodcha); 
  server.on("/getdata", handle_getdata); 
  server.on("/reset", handle_reset); 
  server.onNotFound(handle_NotFound); 
  server.begin();
  Serial.println("Servidor HTTP iniciado");

  ///////////////////////////////
  ArduinoOTA.setPassword("pac2ram");
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else {  // U_SPIFFS
        type = "filesystem";
      }

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) {
        Serial.println("Auth Failed");
      } else if (error == OTA_BEGIN_ERROR) {
        Serial.println("Begin Failed");
      } else if (error == OTA_CONNECT_ERROR) {
        Serial.println("Connect Failed");
      } else if (error == OTA_RECEIVE_ERROR) {
        Serial.println("Receive Failed");
      } else if (error == OTA_END_ERROR) {
        Serial.println("End Failed");
      }
    });

  ArduinoOTA.begin();
  Serial.println("OTA Ready");

  getpositions();

  // xTaskCreate(blink1, "Blink 1", 2048, NULL, 1,ULL);
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  if ((millis() - timegetpositions) > timedelay120)
    {
    getpositions();  
    }
  if ((millis() - time1) > timedelay1)
    {
    cosascadasegundo();  
    }
}