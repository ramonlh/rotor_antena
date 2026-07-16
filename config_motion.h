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


// ---------------------------------------------------------
// TIEMPOS DE SEGURIDAD
// ---------------------------------------------------------

// Margen adicional para arranque, relés, inercia y frenado.
constexpr unsigned long kMoveMarginMs = 5000UL;

// Límite absoluto de cualquier maniobra.
constexpr unsigned long kAbsoluteMoveTimeoutMs = 240000UL;


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