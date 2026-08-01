#include "PollingService.h"

#include <ESP8266WiFi.h>
#include <Updater.h>
#include <time.h>

#include "JsonValueNormalizer.h"
#include "DiagnosticLog.h"
#include "../ProjectConfig.h"

PollingService::PollingService(
    Settings &settings, JsonDocument &liveJson, EpeverController &controller,
    DeviceClockService &deviceClock, WebSocketController &webSocket,
    MqttService &mqtt, TemperatureSensorService &temperatureSensors,
    bool &setNtpTimeToDevice)
    : _settings(settings),
      _liveJson(liveJson),
      _controller(controller),
      _deviceClock(deviceClock),
      _webSocket(webSocket),
      _mqtt(mqtt),
      _temperatureSensors(temperatureSensors),
      _setNtpTimeToDevice(setNtpTimeToDevice)
{
}

bool PollingService::run()
{
  const unsigned long now = millis();
  if (now - _pollTimer < ProjectConfig::PollIntervalMs)
    return true;

  _liveJson["Wifi_RSSI"] = WiFi.RSSI();
  if (strlen(_settings.data.NTPTimezone) > 0 && _setNtpTimeToDevice)
    applyNtpTime();

  if (_controller.read(_requestedDevice))
  {
    _controller.updateJson(_requestedDevice);
    _temperatureSensors.updateJson(_liveJson);
    normalizeJsonNumbers(_liveJson, 2);
#if defined(EPEVER_WEBSERIAL_DATA_LOG) && EPEVER_WEBSERIAL_DATA_LOG
    char deviceKey[8];
    char logLabel[48];
    snprintf(deviceKey, sizeof(deviceKey), "EP_%u", _requestedDevice);
    snprintf(logLabel, sizeof(logLabel),
             "[EPEVER:%u] Received data:", _requestedDevice);
    DiagnosticLog::json(
        logLabel, _liveJson[deviceKey].as<JsonVariantConst>());
#endif
    _webSocket.notify();
  }
  else if (_controller.errorCode() == 0 ||
           now - _notifyTimer >=
               ProjectConfig::ErrorNotificationIntervalMs)
  {
    _webSocket.notify();
    _notifyTimer = millis();
  }

  if (_mqtt.publishDue(now) && !Update.isRunning())
  {
    _temperatureSensors.requestTemperatures();
    _mqtt.publish();
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
