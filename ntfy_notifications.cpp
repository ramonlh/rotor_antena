#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include "ntfy_notifications.h"
#include "config.h"
#include "sat_favorites.h"
#include "sat_visible_now.h"
#include "sat_fetch.h"
#include "rotor_positioning.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static constexpr int kObservedMax = 32;
static constexpr uint32_t kRetryMs = 60UL * 1000UL;

struct ObservedFavorite {
  long noradId;
  SatVisibleState state;
};

static ObservedFavorite gObserved[kObservedMax];
static SemaphoreHandle_t gNtfyMutex = nullptr;
static volatile bool gNtfyBusy = false;
static bool gPending = false;
static String gPendingTitle;
static String gPendingMessage;
static String gStatus = "Desactivado";
static uint32_t gNextAttemptMs = 0;

static void ensureNtfyMutex() {
  if (gNtfyMutex == nullptr) gNtfyMutex = xSemaphoreCreateMutex();
}

static void lockNtfy() {
  ensureNtfyMutex();
  if (gNtfyMutex != nullptr) xSemaphoreTake(gNtfyMutex, portMAX_DELAY);
}

static void unlockNtfy() {
  if (gNtfyMutex != nullptr) xSemaphoreGive(gNtfyMutex);
}

bool isNtfyConfigured() {
  return strlen(NTFY_SERVER) > 0 && strlen(NTFY_TOPIC) > 0;
}

static int observedIndex(long noradId) {
  for (int i = 0; i < kObservedMax; ++i) {
    if (gObserved[i].noradId == noradId) return i;
  }
  for (int i = 0; i < kObservedMax; ++i) {
    if (gObserved[i].noradId == 0) {
      gObserved[i].noradId = noradId;
      gObserved[i].state = SAT_VIS_UNKNOWN;
      return i;
    }
  }
  return -1;
}

static bool enqueueNtfy(const String& title, const String& message) {
  if (!isNtfyConfigured()) return false;
  lockNtfy();
  // Una única ranura evita acumular avisos obsoletos si no hay conexión.
  gPendingTitle = title;
  gPendingMessage = message;
  gPending = true;
  gStatus = "Pendiente";
  unlockNtfy();
  return true;
}

void observeFavoriteVisibilitySnapshot() {
  if (!isNtfyConfigured()) return;

  String newlyVisible;
  int newlyVisibleCount = 0;
  const int count = getSatFavoritesCount();
  for (int i = 0; i < count; ++i) {
    const SatFavoriteItem item = getSatFavorite(i);
    if (item.noradId <= 0) continue;

    const SatVisibleState current = getVisibleNowState(item.noradId);
    const int index = observedIndex(item.noradId);
    if (index < 0) continue;

    const SatVisibleState previous = gObserved[index].state;
    gObserved[index].state = current;

    if (current == SAT_VIS_VISIBLE && previous != SAT_VIS_VISIBLE) {
      String name = String(item.name.c_str());
      if (name.length() == 0) name = "SAT " + String(item.noradId);
      if (newlyVisible.length() > 0) newlyVisible += "\n";
      newlyVisible += name;
      newlyVisible += " (NORAD ";
      newlyVisible += String(item.noradId);
      newlyVisible += ")";
      ++newlyVisibleCount;
    }
  }

  if (newlyVisibleCount > 0) {
    const String title = newlyVisibleCount == 1
        ? "Satelite visible" : "Satelites visibles";
    enqueueNtfy(title, newlyVisible + "\nVisible ahora desde el rotor.");
  }
}

bool requestNtfyTestNotification() {
  return enqueueNtfy(
      "Prueba del rotor",
      "Las notificaciones ntfy están configuradas correctamente."
  );
}

// Retornar antes de borrar la tarea permite liberar String, HTTPClient y TLS.
static void ntfyWorker() {
  String title;
  String message;

  lockNtfy();
  title = gPendingTitle;
  message = gPendingMessage;
  gPending = false;
  gStatus = "Enviando";
  unlockNtfy();

  String base = NTFY_SERVER;
  while (base.endsWith("/")) base.remove(base.length() - 1);
  const String url = base + "/" + NTFY_TOPIC;

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(10);
  HTTPClient http;
  http.begin(client, url);
  http.setConnectTimeout(3000);
  http.setTimeout(12000);
  http.addHeader("Content-Type", "text/plain; charset=utf-8");
  // Cabeceras ASCII para evitar codificación RFC 2047 en el ESP32.
  http.addHeader("Title", title);
  http.addHeader("Priority", "high");
  http.addHeader("Tags", "satellite,antenna");
  const int code = http.POST(message);
  http.end();

  lockNtfy();
  if (code >= 200 && code < 300) {
    gStatus = "Último envío correcto";
    gNextAttemptMs = 0;
  } else {
    gStatus = "Error HTTP " + String(code);
    // Recuperar el mensaje para un solo reintento posterior.
    if (!gPending) {
      gPendingTitle = title;
      gPendingMessage = message;
      gPending = true;
    }
    gNextAttemptMs = millis() + kRetryMs;
  }
  unlockNtfy();
}

static void ntfyTask(void* parameter) {
  (void)parameter;
  ntfyWorker();
  // Mantener la exclusión hasta que se hayan destruido los objetos de red.
  endN2yoRequest();
  lockNtfy();
  gNtfyBusy = false;
  unlockNtfy();
  vTaskDelete(nullptr);
}

void serviceNtfyNotifications() {
  if (!isNtfyConfigured() || WiFi.status() != WL_CONNECTED || gNtfyBusy) return;
  if (isAnyMotionBusy() || isPositionsRequestInProgress() ||
      isVisibleNowRefreshInProgress() || isFavoriteVisibilityRefreshInProgress()) return;
  if (gNextAttemptMs != 0 && (int32_t)(gNextAttemptMs - millis()) > 0) return;

  lockNtfy();
  if (!gPending) {
    unlockNtfy();
    return;
  }
  // Reservar antes de crear la tarea: comprobar únicamente los indicadores
  // no impide que /positions arranque mientras ntfy está enviando.
  if (!tryBeginN2yoRequest()) {
    unlockNtfy();
    return;
  }
  gNtfyBusy = true;
  unlockNtfy();

  if (xTaskCreate(ntfyTask, "ntfy", 6144, nullptr, 1, nullptr) != pdPASS) {
    endN2yoRequest();
    lockNtfy();
    gNtfyBusy = false;
    gStatus = "No se pudo crear la tarea";
    unlockNtfy();
  }
}

bool isNtfyBusy() {
  return gNtfyBusy;
}

String getNtfyStatus() {
  if (!isNtfyConfigured()) return "Desactivado: configura NTFY_TOPIC";
  lockNtfy();
  const String result = gStatus;
  unlockNtfy();
  return result;
}
