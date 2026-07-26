#pragma once

#define ARDUINOJSON_USE_DOUBLE 0
#define ARDUINOJSON_USE_LONG_LONG 1

#include "epever/ControllerTypes.h"

#define EPEVER_BAUD 115200   // baud rate for modbus
#define EPEVER_DE_RE 5       // connect DE and Re to pin D1
#define LED_PIN 02 //D4 with the LED on Wemos D1 Mini
#define TEMPSENS_PIN 04 // DS18B20 Pin

#ifdef ARDUINO_ESP8266_ESP01
#undef EPEVER_DE_RE
#define EPEVER_DE_RE 0  // ESP01 
#endif

#define MAX_DEVICES 6
#define EPEVER_SERIAL Serial

// DON'T edit version here, place version number in platformio.ini (custom_prog_version) !!!
#define SOFTWARE_VERSION SWVERSION

EpeverProfile detectEpeverProfile(uint8_t device, bool force = false);

bool writeEpeverLoadState(uint8_t device, bool state);

bool writeNcG3ChargeCurrentLimit(uint8_t device, float amps);

uint16_t getEpeverRatedChargeCurrent(uint8_t device);
