#include "DiagnosticLog.h"

#include <stdarg.h>

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

void printf(const char *format, ...)
{
  if (diagnosticOutput == nullptr)
    return;

  char message[192];
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);
  diagnosticOutput->println(message);
}

void json(const char *label, JsonVariantConst value)
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
