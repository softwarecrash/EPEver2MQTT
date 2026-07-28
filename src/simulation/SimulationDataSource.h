#pragma once

#include <Arduino.h>

#include "../epever/ControllerTypes.h"

class SimulationDataSource
{
public:
  static constexpr uint8_t DeviceCount = 3;
  static constexpr uint8_t BatterySettingCount = 15;

  SimulationDataSource();

  void begin();
  bool selfTest();
  bool prepareDevice(uint8_t device);
  bool readInputRegisters(uint8_t device, uint16_t address, uint8_t count,
                          uint16_t *values) const;
  bool readHoldingRegisters(uint8_t device, uint16_t address, uint8_t count,
                            uint16_t *values) const;
  bool readCoils(uint8_t device, uint16_t address, uint8_t count,
                 uint16_t *values) const;
  EpeverProfile profile(uint8_t device) const;
  bool setLoadState(uint8_t device, bool state);
  bool setChargeCurrentLimit(uint8_t device, float amps);
  bool readBatterySettings(uint8_t device, uint16_t *values) const;
  bool writeBatterySettings(uint8_t device, const uint16_t *values);
  bool setClock(const char *dateTime);

private:
  struct Snapshot
  {
    uint16_t pvVoltage = 0;
    uint16_t pvCurrent = 0;
    uint32_t pvPower = 0;
    uint16_t pv2Voltage = 0;
    uint16_t pv2Current = 0;
    uint32_t pv2Power = 0;
    uint16_t loadVoltage = 0;
    uint16_t loadCurrent = 0;
    uint32_t loadPower = 0;
    uint16_t batteryVoltage = 0;
    int16_t batteryCurrent = 0;
    uint16_t batterySoc = 0;
    int16_t batteryTemperature = 0;
    int16_t deviceTemperature = 0;
    uint16_t systemVoltage = 0;
    uint16_t highestPvVoltage = 0;
    uint16_t totalPvCurrent = 0;
    uint32_t totalPvPower = 0;
    uint16_t batteryMaximum = 0;
    uint16_t batteryMinimum = 0;
    uint32_t consumedDay = 0;
    uint32_t generatedDay = 0;
    bool daytime = false;
  };

  static void setUint32(uint16_t *values, uint8_t index, uint32_t value);
  uint32_t simulatedTime() const;
  static float clamp(float value, float minimum, float maximum);

  bool _loadState[DeviceCount + 1];
  float _chargeCurrentLimit[DeviceCount + 1];
  float _generatedToday[DeviceCount + 1];
  float _consumedToday[DeviceCount + 1];
  uint16_t _batterySettings[DeviceCount + 1][BatterySettingCount];
  unsigned long _lastUpdate[DeviceCount + 1];
  int32_t _clockOffset;
  uint8_t _preparedDevice = 0;
  Snapshot _snapshot;
};
