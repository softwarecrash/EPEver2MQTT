#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>

#include "../Settings.h"

class NetworkManager
{
public:
  NetworkManager(AsyncWebServer &server, DNSServer &dns,
                 Settings &settings);

  bool connect();

private:
  void configureStaticAddress(AsyncWiFiManager &manager);

  AsyncWebServer &_server;
  DNSServer &_dns;
  Settings &_settings;
  bool _saveRequested = false;
};
