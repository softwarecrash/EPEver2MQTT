#include "WebUiRoutes.h"

#include <AsyncJson.h>

#include "../html.h"

namespace
{
template <size_t Size>
bool copyJsonString(JsonObjectConst source, const char *key, char (&target)[Size])
{
  if (!source[key].is<const char *>())
    return false;

  const char *value = source[key].as<const char *>();
  if (value == nullptr || strlen(value) >= Size || strchr(value, '%') != nullptr)
    return false;

  strlcpy(target, value, Size);
  return true;
}

void addSettings(JsonObject object, const Settings::Data &settings)
{
  object["deviceName"] = settings.deviceName;
  object["deviceQuantity"] = settings.deviceQuantity;
  object["mqttServer"] = settings.mqttServer;
  object["mqttPort"] = settings.mqttPort;
  object["mqttUser"] = settings.mqttUser;
  object["mqttPassword"] = settings.mqttPassword;
  object["mqttTopic"] = settings.mqttTopic;
  object["mqttRefresh"] = settings.mqttRefresh;
  object["mqttTriggerPath"] = settings.mqttTriggerPath;
  object["mqttJson"] = settings.mqttJson;
  object["haDiscovery"] = settings.haDiscovery;
  object["webUiDarkMode"] = settings.webUIdarkmode;
  object["ledBrightness"] = settings.LEDBrightness;
  object["httpUser"] = settings.httpUser;
  object["httpPassword"] = settings.httpPass;
  object["ntpTimezone"] = settings.NTPTimezone;
  object["ntpServer"] = settings.NTPServer;
  object["staticIp"] = settings.staticIP;
  object["staticGateway"] = settings.staticGW;
  object["staticSubnet"] = settings.staticSN;
  object["staticDns"] = settings.staticDNS;
}
}

WebUiRoutes::WebUiRoutes(AsyncWebServer &server, Settings &settings,
                         JsonDocument &liveJson, DeviceClockService &deviceClock,
                         bool &restartNow,
                         unsigned long &restartTimer, uint8_t maximumDevices,
                         const char *softwareVersion)
    : _server(server),
      _settings(settings),
      _liveJson(liveJson),
      _deviceClock(deviceClock),
      _restartNow(restartNow),
      _restartTimer(restartTimer),
      _maximumDevices(maximumDevices),
      _softwareVersion(softwareVersion)
{
}

void WebUiRoutes::registerRoutes()
{
  _server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request)
             {
               if (!authorize(request))
                 return;
               request->send_P(200, "text/html", HTML_MAIN);
             });

  _server.on("/settings", HTTP_GET, [this](AsyncWebServerRequest *request)
             {
               if (!authorize(request))
                 return;
               request->send_P(200, "text/html", HTML_SETTINGS);
             });

  _server.on("/settingsedit", HTTP_GET, [this](AsyncWebServerRequest *request)
             {
               if (!authorize(request))
                 return;
               request->send_P(200, "text/html", HTML_SETTINGS_EDIT);
             });

  _server.on("/confirmreset", HTTP_GET, [this](AsyncWebServerRequest *request)
             {
               if (!authorize(request))
                 return;
               request->send_P(200, "text/html", HTML_CONFIRM_RESET);
             });

  _server.on("/reboot", HTTP_GET, [this](AsyncWebServerRequest *request)
             {
               if (!authorize(request))
                 return;
               request->send_P(200, "text/html", HTML_REBOOT);
             });

  _server.on("/api/reboot", HTTP_POST,
             [this](AsyncWebServerRequest *request)
             {
               if (!authorize(request))
                 return;
               _restartNow = true;
               _restartTimer = millis();
               request->send(202, "application/json",
                             "{\"ok\":true,\"message\":\"Reboot scheduled\"}");
             });

  _server.on("/livejson", HTTP_GET, [this](AsyncWebServerRequest *request)
             {
               if (!authorize(request))
                 return;
               AsyncResponseStream *response =
                   request->beginResponseStream("application/json");
               serializeJson(_liveJson, *response);
               request->send(response);
             });

  _server.on("/api/system", HTTP_GET, [this](AsyncWebServerRequest *request)
             {
               if (authorize(request))
                 sendSystemJson(request);
             });

  _server.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest *request)
             {
               if (authorize(request))
                 sendSettingsJson(request);
             });

  auto *saveHandler = new AsyncCallbackJsonWebHandler(
      "/api/settings",
      [this](AsyncWebServerRequest *request, JsonVariant &json)
      {
        if (authorize(request))
          saveSettingsJson(request, json);
      });
  saveHandler->setMethod(HTTP_POST);
  saveHandler->setMaxContentLength(4096);
  _server.addHandler(saveHandler);

  auto *timeHandler = new AsyncCallbackJsonWebHandler(
      "/api/device-time",
      [this](AsyncWebServerRequest *request, JsonVariant &json)
      {
        if (authorize(request))
          setDeviceTime(request, json);
      });
  timeHandler->setMethod(HTTP_POST);
  timeHandler->setMaxContentLength(256);
  _server.addHandler(timeHandler);
}

bool WebUiRoutes::authorize(AsyncWebServerRequest *request) const
{
  if (strlen(_settings.data.httpUser) == 0 ||
      request->authenticate(_settings.data.httpUser, _settings.data.httpPass))
    return true;

  request->requestAuthentication();
  return false;
}

void WebUiRoutes::sendSystemJson(AsyncWebServerRequest *request) const
{
  JsonDocument document;
  document["deviceName"] = _settings.data.deviceName;
  document["softwareVersion"] = _softwareVersion;
  document["flashSize"] = ESP.getFreeSketchSpace();
  document["darkMode"] = _settings.data.webUIdarkmode;
#ifdef EPEVER_SIMULATION
  document["simulation"] = true;
#else
  document["simulation"] = false;
#endif

  AsyncResponseStream *response =
      request->beginResponseStream("application/json");
  serializeJson(document, *response);
  request->send(response);
}

void WebUiRoutes::sendSettingsJson(AsyncWebServerRequest *request) const
{
  JsonDocument document;
  addSettings(document.to<JsonObject>(), _settings.data);

  AsyncResponseStream *response =
      request->beginResponseStream("application/json");
  serializeJson(document, *response);
  request->send(response);
}

void WebUiRoutes::saveSettingsJson(AsyncWebServerRequest *request,
                                   JsonVariant &json)
{
  if (!json.is<JsonObject>())
  {
    sendJsonStatus(request, 400, false, "JSON object required");
    return;
  }

  JsonObjectConst source = json.as<JsonObjectConst>();
  Settings::Data updated = _settings.data;
  const bool stringsValid =
      copyJsonString(source, "deviceName", updated.deviceName) &&
      copyJsonString(source, "mqttServer", updated.mqttServer) &&
      copyJsonString(source, "mqttUser", updated.mqttUser) &&
      copyJsonString(source, "mqttPassword", updated.mqttPassword) &&
      copyJsonString(source, "mqttTopic", updated.mqttTopic) &&
      copyJsonString(source, "mqttTriggerPath", updated.mqttTriggerPath) &&
      copyJsonString(source, "httpUser", updated.httpUser) &&
      copyJsonString(source, "httpPassword", updated.httpPass) &&
      copyJsonString(source, "ntpTimezone", updated.NTPTimezone) &&
      copyJsonString(source, "ntpServer", updated.NTPServer) &&
      copyJsonString(source, "staticIp", updated.staticIP) &&
      copyJsonString(source, "staticGateway", updated.staticGW) &&
      copyJsonString(source, "staticSubnet", updated.staticSN) &&
      copyJsonString(source, "staticDns", updated.staticDNS);

  if (!stringsValid)
  {
    sendJsonStatus(request, 422, false,
                   "Missing, oversized or invalid string setting");
    return;
  }

  const long deviceQuantity = source["deviceQuantity"] | 0;
  const long mqttPort = source["mqttPort"] | -1;
  const long mqttRefresh = source["mqttRefresh"] | 0;
  const long ledBrightness = source["ledBrightness"] | -1;
  if (deviceQuantity < 1 || deviceQuantity > _maximumDevices ||
      mqttPort < 0 || mqttPort > 65535 ||
      mqttRefresh < 1 || mqttRefresh > 65529 ||
      ledBrightness < 0 || ledBrightness > 255)
  {
    sendJsonStatus(request, 422, false, "Numeric setting out of range");
    return;
  }

  if (!source["mqttJson"].is<bool>() ||
      !source["haDiscovery"].is<bool>() ||
      !source["webUiDarkMode"].is<bool>())
  {
    sendJsonStatus(request, 422, false, "Boolean setting required");
    return;
  }

  updated.deviceQuantity = deviceQuantity;
  updated.mqttPort = mqttPort;
  updated.mqttRefresh = mqttRefresh;
  updated.LEDBrightness = ledBrightness;
  updated.mqttJson = source["mqttJson"].as<bool>();
  updated.haDiscovery = source["haDiscovery"].as<bool>();
  updated.webUIdarkmode = source["webUiDarkMode"].as<bool>();

  _settings.data = updated;
  _settings.save();
  sendJsonStatus(request, 200, true, "Settings saved");
}

void WebUiRoutes::sendJsonStatus(AsyncWebServerRequest *request,
                                 uint16_t statusCode, bool ok,
                                 const char *message) const
{
  JsonDocument document;
  document["ok"] = ok;
  document["message"] = message;
  document["rebootRequired"] = ok;
  String payload;
  serializeJson(document, payload);
  request->send(statusCode, "application/json", payload);
}

void WebUiRoutes::setDeviceTime(AsyncWebServerRequest *request,
                                JsonVariant &json)
{
  const char *dateTime = json["datetime"] | nullptr;
  if (dateTime == nullptr)
  {
    sendJsonStatus(request, 400, false, "datetime is required");
    return;
  }

  const bool success =
      _deviceClock.setAll(dateTime, _settings.data.deviceQuantity);
  sendJsonStatus(request, success ? 200 : 502, success,
                 success ? "Device time updated"
                         : "One or more devices could not be updated");
}
