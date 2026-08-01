#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "../Settings.h"
#include "../epever/DeviceClockService.h"
#include "../epever/EpeverController.h"
#include "../mqtt/MqttService.h"
#include "../web/WebSocketController.h"
#include "TemperatureSensorService.h"

class PollingService
{
public:
  PollingService(Settings &settings, JsonDocument &liveJson,
                 EpeverController &controller,
                 DeviceClockService &deviceClock,
                 WebSocketController &webSocket, MqttService &mqtt,
                 TemperatureSensorService &temperatureSensors,
                 bool &setNtpTimeToDevice);

  bool run();

private:
  void applyNtpTime();

  Settings &_settings;
  JsonDocument &_liveJson;
  EpeverController &_controller;
  DeviceClockService &_deviceClock;
  WebSocketController &_webSocket;
  MqttService &_mqtt;
  TemperatureSensorService &_temperatureSensors;
  bool &_setNtpTimeToDevice;
  uint8_t _requestedDevice = 1;
  unsigned long _notifyTimer = 0;
  unsigned long _pollTimer = 0;
};
