#pragma once
#include <Arduino.h>

struct VisibleSatItem {
  long noradId;
  String name;
};

bool searchVisibleNow();
int getVisibleNowCount();
VisibleSatItem getVisibleNowItem(int index);
void clearVisibleNow();
bool isVisibleNow(long noradId);
