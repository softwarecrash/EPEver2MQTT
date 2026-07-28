#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace DiagnosticLog
{
void begin(Print &output);
void println(const String &message);
void json(const String &label, JsonVariantConst value);
}
