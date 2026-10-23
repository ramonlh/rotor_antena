#pragma once
#include <Arduino.h>

// Encola la descarga; nunca realiza HTTPS en el llamador.
bool initPositionsService();
void getPositions();
void servicePositions();
bool isPositionsRequestInProgress();
uint32_t getLastPredictionAgeMs();
uint32_t getPositionsRetryRemainingMs();
String getLastPredictionError();
void resetPositionsStatus();

// Exclusión mutua de red compartida por N2YO, SatNOGS y ntfy.
// Evita conexiones TLS simultáneas y protege el búfer HTTP compartido.
bool tryBeginN2yoRequest();
void endN2yoRequest();
bool isN2yoRequestBusy();
