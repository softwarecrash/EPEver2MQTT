#pragma once

#ifndef ARDUINOJSON_USE_DOUBLE
#define ARDUINOJSON_USE_DOUBLE 0
#endif
#ifndef ARDUINOJSON_USE_LONG_LONG
#define ARDUINOJSON_USE_LONG_LONG 1
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DallasTemperature.h>
#include <ModbusMaster.h>
#include <UnixTime.h>

#include "../Settings.h"
#include "ControllerTypes.h"

#ifdef EPEVER_SIMULATION
class SimulationDataSource;
#endif

class EpeverController
{
public:
  static constexpr uint8_t MaximumDevices = 6;

  EpeverController(ModbusMaster &modbus, JsonDocument &liveJson,
                   Settings &settings, UnixTime &unixTime,
                   DallasTemperature &temperatureSensors,
                   uint8_t &temperatureSensorCount,
                   uint8_t *temperatureAddress, int &errorCode,
                   const char *softwareVersion);
#ifdef EPEVER_SIMULATION
  EpeverController(ModbusMaster &modbus, JsonDocument &liveJson,
                   Settings &settings, UnixTime &unixTime,
                   DallasTemperature &temperatureSensors,
                   uint8_t &temperatureSensorCount,
                   uint8_t *temperatureAddress, int &errorCode,
                   const char *softwareVersion,
                   SimulationDataSource &simulation);
#endif

  EpeverProfile detectProfile(uint8_t device, bool force = false);
  bool read(uint8_t device);
  bool updateJson(uint8_t device);
  bool writeLoadState(uint8_t device, bool state);
  bool writeChargeCurrentLimit(uint8_t device, float amps);
  uint16_t ratedChargeCurrent(uint8_t device) const;

private:
  static uint32_t wordsToUint32(uint16_t lowWord, uint16_t highWord);
  bool readInputBlock(uint16_t address, uint8_t count, uint16_t *values);
  bool readHoldingBlock(uint16_t address, uint8_t count, uint16_t *values);
  bool readLegacy(uint8_t device);
  bool readNcG3(uint8_t device, EpeverProfile profile);
  bool updateNcG3Json(uint8_t device, EpeverProfile profile);
  static const char *ncG3BatteryVoltageState(uint8_t value);
  static const char *ncG3BatteryTemperatureState(uint8_t value);

  ModbusMaster &_modbus;
  JsonDocument &_liveJson;
  Settings &_settings;
  UnixTime &_unixTime;
  DallasTemperature &_temperatureSensors;
  uint8_t &_temperatureSensorCount;
  uint8_t *_temperatureAddress;
  int &_errorCode;
  const char *_softwareVersion;
#ifdef EPEVER_SIMULATION
  SimulationDataSource *_simulation = nullptr;
#endif
};
