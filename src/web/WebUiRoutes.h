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
#include "../epever/DeviceClockService.h"

class WebUiRoutes
{
public:
  WebUiRoutes(AsyncWebServer &server, Settings &settings,
              JsonDocument &liveJson, DeviceClockService &deviceClock,
              bool &restartNow,
              unsigned long &restartTimer, uint8_t maximumDevices,
              const char *softwareVersion);

  void registerRoutes();

private:
  bool authorize(AsyncWebServerRequest *request) const;
  void sendSystemJson(AsyncWebServerRequest *request) const;
  void sendSettingsJson(AsyncWebServerRequest *request) const;
  void saveSettingsJson(AsyncWebServerRequest *request, JsonVariant &json);
  void setDeviceTime(AsyncWebServerRequest *request, JsonVariant &json);
  void sendJsonStatus(AsyncWebServerRequest *request, uint16_t statusCode,
                      bool ok, const char *message) const;

  AsyncWebServer &_server;
  Settings &_settings;
  JsonDocument &_liveJson;
  DeviceClockService &_deviceClock;
  bool &_restartNow;
  unsigned long &_restartTimer;
  uint8_t _maximumDevices;
  const char *_softwareVersion;
};
