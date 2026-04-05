

#include "state_services.h"

HardwareSerial mySerial(1);
WebServer server(kWebServerPort);
UnixTime stamp(kTimeZoneHours);