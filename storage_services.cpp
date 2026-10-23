#include <Arduino.h>
#include <FFat.h>
#include "storage_services.h"
#include "config.h"

#if ROTOR_ENABLE_FTP
#include <SimpleFTPServer.h>
#endif

static bool gStorageReady = false;
static bool gFtpReady = false;
#if ROTOR_ENABLE_FTP
static FtpServer ftpSrv;
#endif

bool initStorage() {
  if (gStorageReady) return true;

  // Nunca formatear automáticamente: FFat contiene favoritos, cachés y
  // referencias de calibración persistentes.
  if (!FFat.begin(false)) {
    Serial.println("FFat mount failed; filesystem preserved (not formatted)");
    gStorageReady = false;
    return false;
  }

  Serial.println("FFat mounted OK");
  gStorageReady = true;
  return true;
}

bool isStorageReady() {
  return gStorageReady;
}

bool initFtpServer() {
#if !ROTOR_ENABLE_FTP
  gFtpReady = false;
  Serial.println("FTP desactivado en compilacion");
  return false;
#else
  if (!gStorageReady) {
    Serial.println("FTP no iniciado: FFat no esta listo");
    return false;
  }

  if (gFtpReady) return true;

  ftpSrv.begin(FTP_USERNAME, FTP_PASSWORD);
  Serial.println("FTP server started");
  gFtpReady = true;
  return true;
#endif
}

void handleFtpServer() {
#if ROTOR_ENABLE_FTP
  if (!gFtpReady) return;
  ftpSrv.handleFTP();
#endif
}
