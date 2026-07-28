#pragma once

#include <ArduinoJson.h>
#include <DallasTemperature.h>

class TemperatureSensorService
{
public:
  explicit TemperatureSensorService(DallasTemperature &sensors);

  void begin();
  void requestTemperatures();
  void updateJson(JsonDocument &document);

private:
  DallasTemperature &_sensors;
  uint8_t _sensorCount = 0;
  DeviceAddress _address = {};
};
