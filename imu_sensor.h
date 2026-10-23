#pragma once
#include <Arduino.h>
void initImu();
void updateAntennaPosition();
void setImuAzimuthMotionDirection(int8_t direction);
void setImuAzimuthReference(float azDeg);
bool isImuAzimuthReferenceSet();
bool restoreImuAzimuthReference();
void clearImuAzimuthReference(bool eraseStored = true);
float getImuAzimuthRateDps();
float getImuRawElevationDeg();
void setImuElevationOffset(float offsetDeg);
void saveImuElevationOffset(float offsetDeg);
bool saveAzLeftLimitReference(float azLeft);
bool saveElLowerLimitReference(float elLow);
bool saveRotorReferenceLimits(float azLeft, float elLow);
float getAzLeftLimitReference();
float getElLowerLimitReference();
bool hasAzLeftLimitReference();
bool hasElLowerLimitReference();

// true mientras el giróscopo detecta giro manual horizontal de AZ.
bool isImuManualAzimuthMoving();

// Diagnóstico de recepción serie del JY901.
uint32_t getImuSampleAgeMs();
bool isImuDataHealthy();
