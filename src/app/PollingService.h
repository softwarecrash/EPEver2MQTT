#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DallasTemperature.h>

#include "../Settings.h"
#include "../epever/DeviceClockService.h"
#include "../epever/EpeverController.h"
#include "../mqtt/MqttService.h"
#include "../web/WebSocketController.h"

class PollingService
{
public:
  PollingService(Settings &settings, JsonDocument &liveJson,
                 EpeverController &controller,
                 DeviceClockService &deviceClock,
                 WebSocketController &webSocket, MqttService &mqtt,
                 DallasTemperature &temperatureSensors,
                 bool &setNtpTimeToDevice, int &errorCode,
                 uint8_t &requestedDevice, unsigned long &mqttTimer,
                 unsigned long &notifyTimer, unsigned long &pollTimer);

  bool run();

private:
  void applyNtpTime();

  Settings &_settings;
  JsonDocument &_liveJson;
  EpeverController &_controller;
  DeviceClockService &_deviceClock;
  WebSocketController &_webSocket;
  MqttService &_mqtt;
  DallasTemperature &_temperatureSensors;
  bool &_setNtpTimeToDevice;
  int &_errorCode;
  uint8_t &_requestedDevice;
  unsigned long &_mqttTimer;
  unsigned long &_notifyTimer;
  unsigned long &_pollTimer;
};
