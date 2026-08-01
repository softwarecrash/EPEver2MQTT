#pragma once

#include <ArduinoJson.h>

// Rounds floating-point leaves while preserving integers, booleans, strings,
// arrays and objects. Controller measurements have at most 0.01 resolution.
void normalizeJsonNumbers(JsonDocument &document, uint8_t decimalPlaces = 2);
