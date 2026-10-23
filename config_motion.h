#pragma once

#include <Arduino.h>

// ---------------------------------------------------------
// COMPATIBILIDAD TEMPORAL
// Se eliminará cuando ya no quede ningún uso antiguo.
// ---------------------------------------------------------
constexpr unsigned long kMoveTimeoutMs = 30000UL;


// ---------------------------------------------------------
// WATCHDOG DE MOVIMIENTO
// ---------------------------------------------------------

// Cambio mínimo del sensor que consideramos movimiento real.
constexpr float kMinProgressDeg = 0.5f;

// Tiempo máximo permitido sin detectar avance suficiente.
constexpr unsigned long kNoProgressTimeoutMs = 3000UL;


// ---------------------------------------------------------
// VELOCIDADES MÍNIMAS ESPERADAS
//
// Deben ser menores que la velocidad real medida del rotor.
// ---------------------------------------------------------

constexpr float kMinAzSpeedDegPerSec = 1.0f;
constexpr float kMinElSpeedDegPerSec = 0.5f;

// Límite software inferior del eje de elevación.
// Evita trabajar normalmente sobre el fin de carrera de 0 grados.
constexpr float kMinElevationDeg = 3.0f;


// ---------------------------------------------------------
// TIEMPOS DE SEGURIDAD
// ---------------------------------------------------------

// Margen adicional para arranque, relés, inercia y frenado.
constexpr unsigned long kMoveMarginMs = 5000UL;

// Límite absoluto de cualquier maniobra.
constexpr unsigned long kAbsoluteMoveTimeoutMs = 420000UL;

// ---------------------------------------------------------
// LÍMITES DE CABLEADO DEL EJE DE AZIMUT
// ---------------------------------------------------------

// Dirección de respaldo si todavía no existe una referencia del final
// izquierdo. En funcionamiento, el centro se calcula como final izquierdo
// + 180 grados.
constexpr float kAzCableCenterDeg = 0.0f;

// Recorrido físico permitido desde el centro calculado. El final izquierdo
// está en -180 grados (AZ 200 con la calibración actual) y el derecho está
// 320 grados después, en +140 grados (AZ 160). Por tanto, el sector
// geográfico abierto 160..200 no es alcanzable y debe rechazarse.
constexpr float kAzCableMinDeg = -180.0f;
constexpr float kAzCableMaxDeg =  140.0f;

// ---------------------------------------------------------
// TIEMPO PERMITIDO SEGÚN DISTANCIA
// ---------------------------------------------------------

inline unsigned long calculateMoveTimeoutMs(
    float distanceDeg,
    float minimumSpeedDegPerSec) {

  if (distanceDeg < 0.0f) {
    distanceDeg = -distanceDeg;
  }

  if (minimumSpeedDegPerSec <= 0.0f) {
    return kAbsoluteMoveTimeoutMs;
  }

  float calculatedMs =
      (distanceDeg / minimumSpeedDegPerSec) * 1000.0f;

  calculatedMs += static_cast<float>(kMoveMarginMs);

  unsigned long result =
      static_cast<unsigned long>(calculatedMs);

  // El tiempo total nunca puede ser menor que el watchdog
  // de falta de movimiento más un pequeño margen.
  const unsigned long minimumAllowedMs =
      kNoProgressTimeoutMs + 1000UL;

  if (result < minimumAllowedMs) {
    result = minimumAllowedMs;
  }

  if (result > kAbsoluteMoveTimeoutMs) {
    result = kAbsoluteMoveTimeoutMs;
  }

  return result;
}
