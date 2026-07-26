#include "PollingService.h"

#include <ESP8266WiFi.h>
#include <Updater.h>
#include <time.h>

#include "JsonValueNormalizer.h"

PollingService::PollingService(
    Settings &settings, JsonDocument &liveJson, EpeverController &controller,
    DeviceClockService &deviceClock, WebSocketController &webSocket,
    MqttService &mqtt, DallasTemperature &temperatureSensors,
    bool &setNtpTimeToDevice, int &errorCode, uint8_t &requestedDevice,
    unsigned long &mqttTimer, unsigned long &notifyTimer,
    unsigned long &pollTimer)
    : _settings(settings),
      _liveJson(liveJson),
      _controller(controller),
      _deviceClock(deviceClock),
      _webSocket(webSocket),
      _mqtt(mqtt),
      _temperatureSensors(temperatureSensors),
      _setNtpTimeToDevice(setNtpTimeToDevice),
      _errorCode(errorCode),
      _requestedDevice(requestedDevice),
      _mqttTimer(mqttTimer),
      _notifyTimer(notifyTimer),
      _pollTimer(pollTimer)
{
}

bool PollingService::run()
{
  if (millis() < _pollTimer + 500)
    return true;

  _liveJson["Wifi_RSSI"] = WiFi.RSSI();
  if (strlen(_settings.data.NTPTimezone) > 0 && _setNtpTimeToDevice)
    applyNtpTime();

  if (_controller.read(_requestedDevice))
  {
    _controller.updateJson(_requestedDevice);
    normalizeJsonNumbers(_liveJson, 2);
    _webSocket.notify();
  }
  else if (_errorCode == 0 || millis() > _notifyTimer + 1000)
  {
    _webSocket.notify();
    _notifyTimer = millis();
  }

  const unsigned long interval =
      static_cast<unsigned long>(_settings.data.mqttRefresh) * 1000UL;
  if ((_mqttTimer == 0 || millis() > _mqttTimer + interval) &&
      !Update.isRunning())
  {
    _temperatureSensors.requestTemperatures();
    _mqtt.publish();
    _mqttTimer = millis();
  }

  _requestedDevice =
      _requestedDevice >= _settings.data.deviceQuantity
          ? 1
          : _requestedDevice + 1;
  _pollTimer = millis();
  return true;
}

void PollingService::applyNtpTime()
{
  time_t now;
  tm localTime;
  time(&now);
  localtime_r(&now, &localTime);
  char dateTime[13];
  snprintf(dateTime, sizeof(dateTime), "%02u%02u%02u%02u%02u%02u",
           (localTime.tm_year - 100) % 100, localTime.tm_mon + 1,
           localTime.tm_mday, localTime.tm_hour, localTime.tm_min,
           localTime.tm_sec);
  _deviceClock.setAll(dateTime, _settings.data.deviceQuantity);
  _setNtpTimeToDevice = false;
}
