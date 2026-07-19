#pragma once

#include <TFT_eSPI.h>

#include "connectivity.h"

// Initialise TFT and touch hardware.
void displayBegin();

// Call every loop iteration. Handles touch, screen updates, and the on-screen
// WiFi setup wizard when no credentials are stored.
void displayUpdate(const SystemSnapshot& snap);

// Apply brightness from current settings (call after saveSettings).
void applyDisplaySettings();

// Force a full redraw on the next displayUpdate call.
void requestDisplayRedraw();

// Show a full-screen message (used by reset_button).
void displayShowMessage(const String& title, const String& subtitle);

// Wake the screen immediately (e.g. on incoming notification) and reset the
// screen-off inactivity timer.  Safe to call when the screen is already on.
void wakeDisplay();
