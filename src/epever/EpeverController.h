#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ModbusMaster.h>
#include <UnixTime.h>

#include "../Settings.h"
#include "../ProjectConfig.h"
#include "ControllerTypes.h"
#include "EpeverData.h"

#ifdef EPEVER_SIMULATION
class SimulationDataSource;
#endif

class EpeverController
{
public:
  static constexpr uint8_t MaximumDevices = ProjectConfig::MaximumDevices;

  EpeverController(ModbusMaster &modbus, JsonDocument &liveJson,
                   Settings &settings, UnixTime &unixTime,
                   const char *softwareVersion);
#ifdef EPEVER_SIMULATION
  EpeverController(ModbusMaster &modbus, JsonDocument &liveJson,
                   Settings &settings, UnixTime &unixTime,
                   const char *softwareVersion,
                   SimulationDataSource &simulation);
#endif

  EpeverProfile detectProfile(uint8_t device, bool force = false);
  bool read(uint8_t device);
  bool updateJson(uint8_t device);
  bool writeLoadState(uint8_t device, bool state);
  bool writeChargeCurrentLimit(uint8_t device, float amps);
  uint16_t ratedChargeCurrent(uint8_t device) const;
  int errorCode() const;

private:
  static uint32_t wordsToUint32(uint16_t lowWord, uint16_t highWord);
  bool readInputBlock(uint16_t address, uint8_t count, uint16_t *values);
  bool readHoldingBlock(uint16_t address, uint8_t count, uint16_t *values);
  bool readCoil(uint16_t address, bool &value);
  bool readLegacy(uint8_t device);
  bool readNcG3(uint8_t device, EpeverProfile profile);
  bool updateNcG3Json(uint8_t device, EpeverProfile profile);
  void synchronizeDeviceClock(uint8_t device);
  uint32_t currentDeviceTime(uint8_t device) const;
  static const char *ncG3BatteryVoltageState(uint8_t value);
  static const char *ncG3BatteryTemperatureState(uint8_t value);

  ModbusMaster &_modbus;
  JsonDocument &_liveJson;
  Settings &_settings;
  UnixTime &_unixTime;
  int _errorCode = 0;
  const char *_softwareVersion;
  RtcRegisters rtc = {};
  LegacyLiveRegisters live = {};
  LegacyStatisticsRegisters stats = {};
  BatterySettingRegisters settingParam = {};
  LegacyBatteryStatus status_batt = {};
  NcG3Data ncG3 = {};
  EpeverProfile deviceProfiles[MaximumDevices + 1] = {};
  uint16_t deviceModelIds[MaximumDevices + 1] = {};
  uint16_t deviceRatedChargeCurrent[MaximumDevices + 1] = {};
  uint16_t batterySOC = 0;
  int32_t batteryCurrent = 0;
  int16_t batteryTemperature = 0;
  int16_t deviceTemperature = 0;
  uint8_t charger_input = 0;
  uint8_t charger_mode = 0;
  uint8_t result = 0;
  bool loadState = false;
  uint32_t _deviceClockBase[MaximumDevices + 1] = {};
  uint32_t _deviceClockLastReported[MaximumDevices + 1] = {};
  unsigned long _deviceClockSyncMillis[MaximumDevices + 1] = {};
#ifdef EPEVER_SIMULATION
  SimulationDataSource *_simulation = nullptr;
  uint8_t _selectedDevice = 0;
#endif
};
