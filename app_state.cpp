

#include "app_state.h"

bool simulationMode = true;
bool elevMoveActive = false;
bool elevMoveDirectionUp = true;
float elevTarget = 0.0f;
unsigned long elevMoveStartMs = 0;

bool azMoveActive = false;
bool azMoveDirectionRight = true;
bool azMovePhase2 = false;
float azTarget = 0.0f;
unsigned long azMoveStartMs = 0;

bool orientMoveActive = false;
uint8_t orientPhase = 0;
float orientTargetAz = 0.0f;
float orientTargetEl = 0.0f;
bool motionStoppedByUser = false;

float simTargetAz = 0.0f;
float simTargetEl = 0.0f;
float simAzSpeedDegPerSec = 20.0f;   // 30 grados/segundo
float simElSpeedDegPerSec = 10.0f;   // 15 grados/segundo
unsigned long simLastUpdateMs = 0;

const uint32_t kPredictionWindowSec = 30;
