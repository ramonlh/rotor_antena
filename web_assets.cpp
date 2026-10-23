#include <Arduino.h>
#include "app_state.h"
#include "web_assets.h"
#include "web_assets_data.h"

static void sendGzipHtml(const uint8_t* data, size_t length) {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Vary", "Accept-Encoding");
  server.send_P(
      200,
      PSTR("text/html; charset=utf-8"),
      reinterpret_cast<PGM_P>(data),
      length
  );
}

void sendEmbeddedIndexHtml() {
  sendGzipHtml(web_index_gz, web_index_gz_len);
}

void sendEmbeddedConfigHtml() {
  sendGzipHtml(web_config_gz, web_config_gz_len);
}

void sendEmbeddedCalibrationHtml() {
  sendGzipHtml(web_calibracion_gz, web_calibracion_gz_len);
}
