#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "../Settings.h"
#include "../app/PollingControl.h"
#include "../epever/BatterySettingsService.h"
#include "../epever/EpeverController.h"

class MqttService;

class MpptSettingsRoutes
{
public:
  MpptSettingsRoutes(AsyncWebServer &server, Settings &settings,
                     BatterySettingsService &batterySettings,
                     EpeverController &controller,
                     PollingControl &pollingControl, MqttService &mqtt,
                     uint8_t maximumDevices);

  void registerRoutes();

private:
  bool authorize(AsyncWebServerRequest *request);
  bool validDevice(uint8_t device) const;
  uint8_t deviceQuantity() const;

  void handleRead(AsyncWebServerRequest *request);
  void handleApply(AsyncWebServerRequest *request, JsonVariant &json);
  void handleClone(AsyncWebServerRequest *request, JsonVariant &json);

  bool parseSetting(JsonObjectConst json, uint8_t index,
                    uint16_t scale, uint16_t maximum, uint16_t &value);
  void sendResponse(AsyncWebServerRequest *request, uint16_t statusCode,
                    uint8_t device, EpeverProfile profile,
                    const BatterySettingRegisters *settings,
                    const String &message);
  static const char *validationMessage(
      BatterySettingsService::ValidationError error);

  AsyncWebServer &_server;
  Settings &_settings;
  BatterySettingsService &_batterySettings;
  EpeverController &_controller;
  PollingControl &_pollingControl;
  MqttService &_mqtt;
  uint8_t _maximumDevices;
};
