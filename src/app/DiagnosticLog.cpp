#include "DiagnosticLog.h"

namespace
{
Print *diagnosticOutput = nullptr;
}

namespace DiagnosticLog
{
void begin(Print &output)
{
  diagnosticOutput = &output;
}

void println(const String &message)
{
  if (diagnosticOutput != nullptr)
    diagnosticOutput->println(message);
}

void json(const String &label, JsonVariantConst value)
{
  if (diagnosticOutput == nullptr)
    return;

  // Keep one complete sample in one WebSocket message. Pretty-printed JSON
  // would enqueue one message per line and can overwhelm the ESP8266 at the
  // normal polling rate.
  diagnosticOutput->print(label);
  diagnosticOutput->print(' ');
  serializeJson(value, *diagnosticOutput);
  diagnosticOutput->println();
}
}
