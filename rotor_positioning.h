

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

void stopAllMotion();
bool isAnyMotionBusy();

const char* getMotionStatusText();

void updateSimulationMotion();
