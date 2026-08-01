#pragma once

#include <Arduino.h>
#include <ModbusMaster.h>

#include "../app/PollingControl.h"
#include "EpeverController.h"

#ifdef EPEVER_SIMULATION
class SimulationDataSource;
#endif

class DeviceClockService
{
public:
  DeviceClockService(ModbusMaster &modbus, PollingControl &pollingControl,
                     EpeverController &controller);
#ifdef EPEVER_SIMULATION
  DeviceClockService(ModbusMaster &modbus, PollingControl &pollingControl,
                     EpeverController &controller,
                     SimulationDataSource &simulation);
#endif

  bool setAll(const char *dateTime, uint8_t deviceQuantity);

private:
  static bool parseDateTime(const char *dateTime, uint8_t *parts);

  ModbusMaster &_modbus;
  PollingControl &_pollingControl;
  EpeverController &_controller;
#ifdef EPEVER_SIMULATION
  SimulationDataSource *_simulation = nullptr;
#endif
};
