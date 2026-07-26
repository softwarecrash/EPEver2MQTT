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
