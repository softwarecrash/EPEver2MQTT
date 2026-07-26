#pragma once

#ifndef ARDUINOJSON_USE_DOUBLE
#define ARDUINOJSON_USE_DOUBLE 0
#endif
#ifndef ARDUINOJSON_USE_LONG_LONG
#define ARDUINOJSON_USE_LONG_LONG 1
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "../Settings.h"
#include "../epever/BatterySettingsService.h"

class MpptSettingsRoutes
{
public:
  using DetectProfileFn = EpeverProfile (*)(uint8_t device, bool force);

  MpptSettingsRoutes(AsyncWebServer &server, Settings &settings,
                     BatterySettingsService &batterySettings,
                     JsonDocument &liveJson, bool &workerCanRun,
                     unsigned long &mqttTimer, uint8_t maximumDevices,
                     DetectProfileFn detectProfile);

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
  void updateJson(uint8_t device, EpeverProfile profile,
                  const uint16_t *values);
  void sendResponse(AsyncWebServerRequest *request, uint16_t statusCode,
                    uint8_t device, EpeverProfile profile,
                    const uint16_t *values, const String &message);
  static const char *validationMessage(
      BatterySettingsService::ValidationError error);

  AsyncWebServer &_server;
  Settings &_settings;
  BatterySettingsService &_batterySettings;
  JsonDocument &_liveJson;
  bool &_workerCanRun;
  unsigned long &_mqttTimer;
  uint8_t _maximumDevices;
  DetectProfileFn _detectProfile;
};
