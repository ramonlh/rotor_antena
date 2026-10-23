

#pragma once

// Interruptor de compilacion del servidor FTP:
//   0 = desactivado (recomendado para uso normal; ahorra flash y RAM)
//   1 = activado (usar solo durante sesiones de mantenimiento)
// Cambiar este valor y volver a compilar/cargar el firmware.
#define ROTOR_ENABLE_FTP 0

// Notificaciones ntfy. Se pueden sobrescribir en config_secrets.h.
// NTFY_TOPIC vacío deja el servicio desactivado y conserva compatibilidad
// con instalaciones que todavía no tienen estas opciones.
#ifndef NTFY_SERVER
#define NTFY_SERVER "https://ntfy.sh"
#endif
#ifndef NTFY_TOPIC
#define NTFY_TOPIC ""
#endif

static const char* SERVER_POS_BASE = "https://api.n2yo.com/rest/v1/satellite/positions/";
// La ubicación de la estación (MY_LAT, MY_LON, MY_ALT_M) se define en
// config_secrets.h y la usan todas las consultas a N2YO.
