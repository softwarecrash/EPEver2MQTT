#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace DiagnosticLog
{
void begin(Print &output);
void println(const String &message);
void printf(const char *format, ...);
void json(const char *label, JsonVariantConst value);
}
