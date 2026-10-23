#pragma once

// Copiar este archivo como config_secrets.h y sustituir los valores.
// Usar contraseñas diferentes y robustas para AP, FTP y OTA.

static const char* WIFI_SSID = "NOMBRE_WIFI";
static const char* WIFI_PASSWORD = "CONTRASENA_WIFI";

static const char* AP_SSID = "rotorantena";
static const char* AP_PASSWORD = "CONTRASENA_AP";

static const char* FTP_USERNAME = "USUARIO_FTP";
static const char* FTP_PASSWORD = "CONTRASENA_FTP";

static const char* OTA_PASSWORD = "CONTRASENA_OTA";

static const char* N2YO_API_KEY = "CLAVE_API_N2YO";

// Ubicación de la estación (grados decimales, altitud en metros).
static constexpr double MY_LAT = 0.0;
static constexpr double MY_LON = 0.0;
static constexpr int MY_ALT_M = 0;

// Tema largo y difícil de adivinar. Déjalo vacío para desactivar ntfy.
#define NTFY_SERVER "https://ntfy.sh"
#define NTFY_TOPIC "rotor-antena-cambia-este-identificador"
