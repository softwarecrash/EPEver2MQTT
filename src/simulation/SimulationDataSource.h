#pragma once

#ifndef ARDUINOJSON_USE_DOUBLE
#define ARDUINOJSON_USE_DOUBLE 0
#endif
#ifndef ARDUINOJSON_USE_LONG_LONG
#define ARDUINOJSON_USE_LONG_LONG 1
#endif

#include <Arduino.h>
#include <ArduinoJson.h>

#include "../epever/ControllerTypes.h"

class SimulationDataSource
{
public:
  static constexpr uint8_t DeviceCount = 3;
  static constexpr uint8_t BatterySettingCount = 15;

  SimulationDataSource();

  void begin();
  void update(JsonDocument &document);
  EpeverProfile profile(uint8_t device) const;
  bool setLoadState(uint8_t device, bool state);
  bool setChargeCurrentLimit(uint8_t device, float amps);
  bool readBatterySettings(uint8_t device, uint16_t *values) const;
  bool writeBatterySettings(uint8_t device, const uint16_t *values);
  bool setClock(const char *dateTime);

private:
  void addDevice(JsonDocument &document, uint8_t device, float solarFactor,
                 float hours, float elapsedHours);
  static float clamp(float value, float minimum, float maximum);

  bool _loadState[DeviceCount + 1];
  float _chargeCurrentLimit[DeviceCount + 1];
  float _generatedToday[DeviceCount + 1];
  float _consumedToday[DeviceCount + 1];
  uint16_t _batterySettings[DeviceCount + 1][BatterySettingCount];
  unsigned long _lastUpdate;
  int32_t _clockOffset;
};
