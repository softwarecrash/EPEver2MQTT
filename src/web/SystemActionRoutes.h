#pragma once

#ifndef ARDUINOJSON_USE_DOUBLE
#define ARDUINOJSON_USE_DOUBLE 0
#endif
#ifndef ARDUINOJSON_USE_LONG_LONG
#define ARDUINOJSON_USE_LONG_LONG 1
#endif

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "../Settings.h"

class SystemActionRoutes
{
public:
  SystemActionRoutes(AsyncWebServer &server, Settings &settings,
                     bool &factoryResetRequested,
                     bool &discoveryRequested, bool &workerCanRun,
                     bool &restartRequested, unsigned long &restartTimer,
                     HardwareSerial &serial,
                     uint8_t transceiverEnablePin);

  void registerRoutes();

private:
  bool authorize(AsyncWebServerRequest *request) const;
  void setDeviceAddress(AsyncWebServerRequest *request, uint8_t address);

  AsyncWebServer &_server;
  Settings &_settings;
  bool &_factoryResetRequested;
  bool &_discoveryRequested;
  bool &_workerCanRun;
  bool &_restartRequested;
  unsigned long &_restartTimer;
  HardwareSerial &_serial;
  uint8_t _transceiverEnablePin;
};
