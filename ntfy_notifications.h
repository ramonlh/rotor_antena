#pragma once

#include <Arduino.h>

// Observa el resultado de una instantánea completa de N2YO y encola un aviso
// únicamente cuando un favorito pasa de no visible a visible.
void observeFavoriteVisibilitySnapshot();

// Encola un mensaje de prueba solicitado desde la interfaz.
bool requestNtfyTestNotification();

// Arranca los envíos pendientes sin bloquear el control del rotor.
void serviceNtfyNotifications();

bool isNtfyConfigured();
bool isNtfyBusy();
String getNtfyStatus();
