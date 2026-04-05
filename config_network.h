#pragma once
#include <IPAddress.h>

static const char* WIFI_SSID = "TU_WIFI";
static const char* WIFI_PASSWORD = "TU_PASSWORD_WIFI";

static const IPAddress LOCAL_IP(192, 168, 1, 91);
static const IPAddress GATEWAY_IP(192, 168, 1, 1);
static const IPAddress SUBNET_MASK(255, 255, 255, 0);
static const IPAddress PRIMARY_DNS(8, 8, 8, 8);
static const IPAddress SECONDARY_DNS(8, 8, 4, 4);

static const char* AP_SSID = "rotorantena";
static const char* AP_PASSWORD = "TU_PASSWORD_AP";
