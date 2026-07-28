#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "../Settings.h"
#include "../app/ApplicationRequests.h"
#include "../app/PollingControl.h"
#include "../epever/DeviceAddressService.h"

class SystemActionRoutes
{
public:
  SystemActionRoutes(AsyncWebServer &server, Settings &settings,
                     ApplicationRequests &applicationRequests,
                     PollingControl &pollingControl,
                     DeviceAddressService &deviceAddress);

  void registerRoutes();

private:
  bool authorize(AsyncWebServerRequest *request) const;
  void setDeviceAddress(AsyncWebServerRequest *request, uint8_t address);

  AsyncWebServer &_server;
  Settings &_settings;
  ApplicationRequests &_applicationRequests;
  PollingControl &_pollingControl;
  DeviceAddressService &_deviceAddress;
};
