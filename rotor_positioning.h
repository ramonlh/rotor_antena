#pragma once

void setAzimuth(float azim);
void orientAntenna(float azim, float elev);
void jogAzimuth(float delta);
void jogElevation(float delta);

void startAzimuthMove(float azim);
void updateAzimuthMove();
bool isAzimuthBusy();

void startOrientationMove(float azim, float elev);
void updateOrientationMove();
bool isOrientationBusy();

void stopMotionBySafety(const char* reason);
void stopAllMotion();
bool isAnyMotionBusy();
const char* getMotionStatusText();
const char* getLastSafetyStopReason();
void updateSimulationMotion();

// Referenciado por finales que cortan el motor.
bool startAzLeftReference();
bool startAzLeftLimitRelease();
bool wasAzLeftLimitReleaseSuccessful();
bool startElLowerReference();
void updateRotorReference();
bool isRotorReferenceBusy();

bool startAzLeftCalibrationSearch();
bool startElLowerCalibrationSearch();
bool applyAzLeftCalibration(float azDeg);
bool applyElLowerCalibration(float elDeg);
bool isAzLeftCalibrationFound();
bool isElLowerCalibrationFound();
