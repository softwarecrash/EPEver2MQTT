#include "NotificationLed.h"

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "../Settings.h"
#include "../epever/EpeverController.h"

NotificationLed::NotificationLed(uint8_t pin, Settings &settings,
                                 PubSubClient &mqttClient,
                                 EpeverController &controller)
    : _pin(pin),
      _settings(settings),
      _mqttClient(mqttClient),
      _controller(controller),
      _ledOn(false),
      _remainingPulses(0),
      _repeatTimer(0),
      _cycleTimer(0)
{
}

void NotificationLed::begin()
{
  pinMode(_pin, OUTPUT);
  analogWrite(_pin, 255 - _settings.data.LEDBrightness);
}

void NotificationLed::update()
{
  const unsigned long now = millis();
  if (_remainingPulses == 0 && now - _repeatTimer >= RepeatTimeMs)
  {
    if (WiFi.status() != WL_CONNECTED)
      _remainingPulses = 4;
    else if (!_mqttClient.connected() &&
             strlen(_settings.data.mqttServer) > 0)
      _remainingPulses = 3;
    else if (_controller.errorCode() != 0)
      _remainingPulses = 2;
    else
      _remainingPulses = 1;
  }

  if (_remainingPulses > 0 && now - _cycleTimer >= CycleTimeMs)
  {
    _ledOn = !_ledOn;
    if (!_ledOn)
    {
      _remainingPulses--;
      if (_remainingPulses == 0)
        _repeatTimer = now;
    }
    _cycleTimer = now;
  }

  analogWrite(_pin, _ledOn ? 255 - _settings.data.LEDBrightness : 255);
}
