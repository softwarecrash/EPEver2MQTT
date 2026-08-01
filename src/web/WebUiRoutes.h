#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "../Settings.h"
#include "../app/ApplicationRequests.h"
#include "../epever/DeviceClockService.h"

class WebUiRoutes
{
public:
  WebUiRoutes(AsyncWebServer &server, Settings &settings,
              const JsonDocument &liveJson, DeviceClockService &deviceClock,
              ApplicationRequests &applicationRequests,
              uint8_t maximumDevices,
              const char *softwareVersion);

  void registerRoutes();

private:
  bool authorize(AsyncWebServerRequest *request) const;
  void sendSystemJson(AsyncWebServerRequest *request) const;
  void sendSettingsJson(AsyncWebServerRequest *request) const;
  void saveSettingsJson(AsyncWebServerRequest *request, JsonVariant &json);
  void setDeviceTime(AsyncWebServerRequest *request, JsonVariant &json);
  void sendJsonStatus(AsyncWebServerRequest *request, uint16_t statusCode,
                      bool ok, const char *message,
                      bool rebootRequired = false) const;

  AsyncWebServer &_server;
  Settings &_settings;
  const JsonDocument &_liveJson;
  DeviceClockService &_deviceClock;
  ApplicationRequests &_applicationRequests;
  uint8_t _maximumDevices;
  const char *_softwareVersion;
};
