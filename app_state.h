

#pragma once

#include "state_services.h"
#include "state_imu.h"
#include "state_satellite.h"
#include "state_tracking.h"

extern bool simulationMode;
extern bool elevMoveActive;
extern bool elevMoveDirectionUp;
extern float elevTarget;
extern unsigned long elevMoveStartMs;

extern bool azMoveActive;
extern bool azMoveDirectionRight;
extern bool azMovePhase2;
extern float azTarget;
extern unsigned long azMoveStartMs;

extern bool orientMoveActive;
extern uint8_t orientPhase;
extern float orientTargetAz;
extern float orientTargetEl;
static const uint8_t ORIENT_IDLE = 0;
static const uint8_t ORIENT_WAIT_ELEV = 1;
static const uint8_t ORIENT_WAIT_AZ = 2;
extern bool motionStoppedByUser;

extern float simTargetAz;
extern float simTargetEl;
extern float simAzSpeedDegPerSec;
extern float simElSpeedDegPerSec;
extern unsigned long simLastUpdateMs;

extern const uint32_t kPredictionWindowSec;
