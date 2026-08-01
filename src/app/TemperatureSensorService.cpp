#include "TemperatureSensorService.h"

TemperatureSensorService::TemperatureSensorService(
    DallasTemperature &sensors)
    : _sensors(sensors)
{
}

void TemperatureSensorService::begin()
{
  _sensors.begin();
  _sensorCount = _sensors.getDeviceCount();
}

void TemperatureSensorService::requestTemperatures()
{
  _sensors.requestTemperatures();
}

void TemperatureSensorService::updateJson(JsonDocument &document)
{
  for (uint8_t index = 0; index < _sensorCount; index++)
  {
    if (!_sensors.getAddress(_address, index))
      continue;

    char sensorKey[14];
    snprintf(sensorKey, sizeof(sensorKey), "DS18B20_%u", index + 1);
    document[sensorKey] = _sensors.getTempC(_address);
  }
}
