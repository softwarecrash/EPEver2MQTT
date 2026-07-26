#pragma once

#include <Arduino.h>
#include <ModbusMaster.h>

#include "ControllerTypes.h"

#ifdef EPEVER_SIMULATION
class SimulationDataSource;
#endif

class DeviceClockService
{
public:
  using DetectProfileFn = EpeverProfile (*)(uint8_t device, bool force);

  DeviceClockService(ModbusMaster &modbus, bool &workerCanRun,
                     DetectProfileFn detectProfile);
#ifdef EPEVER_SIMULATION
  DeviceClockService(ModbusMaster &modbus, bool &workerCanRun,
                     DetectProfileFn detectProfile,
                     SimulationDataSource &simulation);
#endif

  bool setAll(const char *dateTime, uint8_t deviceQuantity);

private:
  static bool parseDateTime(const char *dateTime, uint8_t *parts);

  ModbusMaster &_modbus;
  bool &_workerCanRun;
  DetectProfileFn _detectProfile;
#ifdef EPEVER_SIMULATION
  SimulationDataSource *_simulation = nullptr;
#endif
};
