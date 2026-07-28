#pragma once

#include <Arduino.h>

class Settings
{
public:
  struct Data
  {
    // This structure is stored byte-for-byte in EEPROM. Append new fields at
    // the end and increment ConfigVersion; never reorder existing fields.
    unsigned int coVers;
    char deviceName[40];         // device name
    char mqttServer[40];
    char mqttUser[40];
    char mqttPassword[40];
    char mqttTopic[40];
    char mqttTriggerPath[80];
    unsigned int mqttPort;
    unsigned int mqttRefresh;
    unsigned int deviceQuantity;
    bool mqttJson;
    bool webUIdarkmode;
    char httpUser[40];           // http basic auth username
    char httpPass[40];           // http basic auth password
    bool haDiscovery;
    char NTPTimezone[40];
    char NTPServer[40];
    byte LEDBrightness;
    char staticIP[16];
    char staticGW[16];
    char staticSN[16];
    char staticDNS[16];
  };

  Data data = {};

  void load();
  void save();
  void reset();

private:
  static constexpr unsigned int ConfigVersion = 12;

  void setDefaults();
  void validate();
};
