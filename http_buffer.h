#pragma once
#include <Arduino.h>
#include <HTTPClient.h>

// Lee el cuerpo en un único búfer reutilizable. El puntero es válido hasta
// la siguiente llamada y debe consumirse antes de liberar el guard de red.
bool readHttpBody(
    HTTPClient& http,
    const char*& data,
    size_t& length,
    bool allowTruncated = false);
size_t getHttpBufferCapacity();
