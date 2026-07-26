#pragma once

#include <Arduino.h>

class PubSubClient;
class Settings;

class NotificationLed
{
public:
  NotificationLed(uint8_t pin, Settings &settings, PubSubClient &mqttClient,
                  int &deviceErrorCode);

  void begin();
  void update();

private:
  static constexpr unsigned long RepeatTimeMs = 5000;
  static constexpr unsigned long CycleTimeMs = 250;

  uint8_t _pin;
  Settings &_settings;
  PubSubClient &_mqttClient;
  int &_deviceErrorCode;
  bool _ledOn;
  uint8_t _remainingPulses;
  unsigned long _repeatTimer;
  unsigned long _cycleTimer;
};
