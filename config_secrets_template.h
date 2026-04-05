#pragma once
/*
  Copia este fichero como "config_secrets.h" si en el futuro quieres sacar
  del repositorio las credenciales y claves.

  Ejemplo de uso:
  1) Renombra o copia este fichero a config_secrets.h
  2) Sustituye los valores por los tuyos
  3) Incluye config_secrets.h desde config_network.h / config_services.h
  4) Añade config_secrets.h al .gitignore
*/

static const char* WIFI_SSID = "TU_WIFI";
static const char* WIFI_PASSWORD = "TU_PASSWORD";
static const char* AP_SSID = "rotorantena";
static const char* AP_PASSWORD = "12341234";
static const char* OTA_PASSWORD = "TU_CLAVE_OTA";
static const char* SERVER_POS_KEY = "/&apiKey=TU_API_KEY_N2YO";
