#include <Arduino.h>
#include <FFat.h>
#include <SimpleFTPServer.h>
#include "storage_services.h"
#include "config.h"

static bool gStorageReady = false;
static bool gFtpReady = false;
static FtpServer ftpSrv;

bool initStorage() {
  if (gStorageReady) return true;

  if (!FFat.begin(true)) {
    Serial.println("FFat mount failed");
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
  if (!gStorageReady) {
    Serial.println("FTP no iniciado: FFat no esta listo");
    return false;
  }

  if (gFtpReady) return true;

  ftpSrv.begin(FTP_USERNAME, FTP_PASSWORD);
  Serial.println("FTP server started");
  gFtpReady = true;
  return true;
}

void handleFtpServer() {
  if (!gFtpReady) return;
  ftpSrv.handleFTP();
}