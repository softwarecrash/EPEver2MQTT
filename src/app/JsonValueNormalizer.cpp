#include "JsonValueNormalizer.h"

#include <math.h>

namespace
{
void normalizeVariant(JsonVariant value, float factor)
{
  if (value.is<JsonObject>())
  {
    for (JsonPair member : value.as<JsonObject>())
      normalizeVariant(member.value(), factor);
    return;
  }

  if (value.is<JsonArray>())
  {
    for (JsonVariant member : value.as<JsonArray>())
      normalizeVariant(member, factor);
    return;
  }

  // ArduinoJson reports integer values as convertible to float as well.
  // Preserve them before normalizing fractional measurements: converting a
  // Unix timestamp around 1.8 billion to float loses roughly seven bits of
  // second-level precision and makes DEVICE_TIME appear stationary.
  if (value.is<int64_t>() || value.is<uint64_t>())
    return;

  if (!value.is<float>())
    return;

  const float number = value.as<float>();
  if (isfinite(number))
    value.set(roundf(number * factor) / factor);
}
}

void normalizeJsonNumbers(JsonDocument &document, uint8_t decimalPlaces)
{
  float factor = 1.0f;
  for (uint8_t index = 0; index < decimalPlaces; index++)
    factor *= 10.0f;
  normalizeVariant(document.as<JsonVariant>(), factor);
}
