#include "MpptSettingsRoutes.h"

#include <AsyncJson.h>
#include <math.h>

#include "../app/JsonValueNormalizer.h"
#include "../html.h"

namespace
{
const char *const LegacyBatteryTypes[] = {
    "User", "Sealed", "GEL", "Flooded"};

const char *const NcG3BatteryTypes[] = {
    "User", "SLA", "GEL", "Flooded", "LFP4S", "LFP8S", "LFP15S",
    "LFP16S", "LNCM3S", "LNCM6S", "LNCM7S", "LNCM13S", "LNCM14S"};

class WorkerPause
{
public:
  explicit WorkerPause(bool &workerCanRun)
      : _workerCanRun(workerCanRun), _previousValue(workerCanRun)
  {
    _workerCanRun = false;
  }

  ~WorkerPause()
  {
    _workerCanRun = _previousValue;
  }

private:
  bool &_workerCanRun;
  bool _previousValue;
};
}

MpptSettingsRoutes::MpptSettingsRoutes(
    AsyncWebServer &server, Settings &settings,
    BatterySettingsService &batterySettings, JsonDocument &liveJson,
    bool &workerCanRun, unsigned long &mqttTimer, uint8_t maximumDevices,
    DetectProfileFn detectProfile)
    : _server(server),
      _settings(settings),
      _batterySettings(batterySettings),
      _liveJson(liveJson),
      _workerCanRun(workerCanRun),
      _mqttTimer(mqttTimer),
      _maximumDevices(maximumDevices),
      _detectProfile(detectProfile)
{
}

void MpptSettingsRoutes::registerRoutes()
{
  _server.on("/mpptsettings", HTTP_GET,
             [this](AsyncWebServerRequest *request)
             {
               if (!authorize(request))
                 return;
               AsyncWebServerResponse *response = request->beginResponse_P(
                   200, "text/html", HTML_MPPT_SETTINGS);
               response->addHeader("Cache-Control",
                                   "no-store, no-cache, must-revalidate");
               request->send(response);
             });

  _server.on("/api/mppt/settings", HTTP_GET,
             [this](AsyncWebServerRequest *request)
             {
               if (authorize(request))
                 handleRead(request);
             });

  auto *applyHandler = new AsyncCallbackJsonWebHandler(
      "/api/mppt/settings",
      [this](AsyncWebServerRequest *request, JsonVariant &json)
      {
        if (authorize(request))
          handleApply(request, json);
      });
  applyHandler->setMethod(HTTP_POST);
  applyHandler->setMaxContentLength(2048);
  _server.addHandler(applyHandler);

  auto *cloneHandler = new AsyncCallbackJsonWebHandler(
      "/api/mppt/clone",
      [this](AsyncWebServerRequest *request, JsonVariant &json)
      {
        if (authorize(request))
          handleClone(request, json);
      });
  cloneHandler->setMethod(HTTP_POST);
  cloneHandler->setMaxContentLength(512);
  _server.addHandler(cloneHandler);
}

bool MpptSettingsRoutes::authorize(AsyncWebServerRequest *request)
{
  if (strlen(_settings.data.httpUser) == 0 ||
      request->authenticate(_settings.data.httpUser,
                            _settings.data.httpPass))
    return true;

  request->requestAuthentication();
  return false;
}

uint8_t MpptSettingsRoutes::deviceQuantity() const
{
  return _settings.data.deviceQuantity > _maximumDevices
             ? _maximumDevices
             : _settings.data.deviceQuantity;
}

bool MpptSettingsRoutes::validDevice(uint8_t device) const
{
  return device >= 1 && device <= deviceQuantity();
}

void MpptSettingsRoutes::handleRead(AsyncWebServerRequest *request)
{
  uint8_t device =
      request->hasParam("device")
          ? request->getParam("device")->value().toInt()
          : 1;
  if (!validDevice(device))
  {
    sendResponse(request, 400, device, EpeverProfile::Unknown, nullptr,
                 "Invalid MPPT device number");
    return;
  }

  uint16_t values[BatterySettingsService::ValueCount];
  EpeverProfile profile;
  uint8_t status;
  {
    WorkerPause pause(_workerCanRun);
    profile = _detectProfile(device, false);
    status = _batterySettings.read(device, profile, values);
  }

  if (status != ModbusMaster::ku8MBSuccess)
  {
    sendResponse(request, 502, device, profile, nullptr,
                 "Modbus read failed (code " + String(status) + ")");
    return;
  }

  updateJson(device, profile, values);
  sendResponse(request, 200, device, profile, values,
               "Settings read successfully");
}

void MpptSettingsRoutes::handleApply(AsyncWebServerRequest *request,
                                     JsonVariant &json)
{
  if (!json.is<JsonObject>())
  {
    sendResponse(request, 400, 0, EpeverProfile::Unknown, nullptr,
                 "JSON object required");
    return;
  }
  JsonObjectConst body = json.as<JsonObjectConst>();
  uint8_t device = body["device"] | 0;
  if (!validDevice(device))
  {
    sendResponse(request, 400, device, EpeverProfile::Unknown, nullptr,
                 "Invalid MPPT device number");
    return;
  }

  EpeverProfile profile;
  {
    WorkerPause pause(_workerCanRun);
    profile = _detectProfile(device, false);
  }
  if (profile == EpeverProfile::Unknown)
  {
    sendResponse(request, 502, device, profile, nullptr,
                 "Unable to detect the controller profile");
    return;
  }

  uint16_t values[BatterySettingsService::ValueCount];
  uint16_t maximumBatteryType = isNcG3Profile(profile) ? 12 : 3;
  if (!parseSetting(body, 0, 1, maximumBatteryType, values[0]) ||
      !parseSetting(body, 1, 1, UINT16_MAX, values[1]) ||
      !parseSetting(body, 2, 100, 900, values[2]))
  {
    sendResponse(
        request, 400, device, profile, nullptr,
        "Battery type, capacity, or temperature compensation is invalid");
    return;
  }

  for (uint8_t index = 3;
       index < BatterySettingsService::ValueCount; index++)
  {
    if (!parseSetting(body, index, 100, UINT16_MAX, values[index]))
    {
      sendResponse(request, 400, device, profile, nullptr,
                   "E" + String(index + 1) + " contains an invalid value");
      return;
    }
  }

  BatterySettingsService::ValidationError validation =
      BatterySettingsService::validate(values);
  if (validation != BatterySettingsService::ValidationError::None)
  {
    sendResponse(request, 400, device, profile, nullptr,
                 validationMessage(validation));
    return;
  }

  uint16_t readBack[BatterySettingsService::ValueCount];
  uint8_t writeStatus;
  uint8_t readStatus;
  BatterySettingsService::VerifyResult verifyResult;
  {
    WorkerPause pause(_workerCanRun);
    verifyResult = _batterySettings.writeAndVerify(
        device, profile, values, readBack, writeStatus, readStatus);
  }

  if (verifyResult ==
      BatterySettingsService::VerifyResult::ReadBackFailed)
  {
    sendResponse(request, 502, device, profile, nullptr,
                 "Write code " + String(writeStatus) +
                     "; read-back failed with code " + String(readStatus));
    return;
  }

  updateJson(device, profile, readBack);
  if (verifyResult == BatterySettingsService::VerifyResult::ValueMismatch)
  {
    sendResponse(
        request, 409, device, profile, readBack,
        "Controller rejected or changed values; actual values were reloaded");
    return;
  }

  _mqttTimer = 0;
  sendResponse(
      request, 200, device, profile, readBack,
      writeStatus == ModbusMaster::ku8MBSuccess
          ? "MPPT settings applied and verified"
          : "Settings verified despite write acknowledgement code " +
                String(writeStatus));
}

void MpptSettingsRoutes::handleClone(AsyncWebServerRequest *request,
                                     JsonVariant &json)
{
  if (!json.is<JsonObject>())
  {
    sendResponse(request, 400, 0, EpeverProfile::Unknown, nullptr,
                 "JSON object required");
    return;
  }
  JsonObjectConst body = json.as<JsonObjectConst>();
  uint8_t source = body["source"] | 0;
  if (!validDevice(source))
  {
    sendResponse(request, 400, source, EpeverProfile::Unknown, nullptr,
                 "Invalid source MPPT device number");
    return;
  }

  uint16_t selectedMask = 0;
  uint8_t targetCount = 0;
  JsonArrayConst targets = body["targets"].as<JsonArrayConst>();
  for (JsonVariantConst entry : targets)
  {
    uint8_t target = entry.as<uint8_t>();
    if (!validDevice(target) || target == source)
    {
      sendResponse(request, 400, source, EpeverProfile::Unknown, nullptr,
                   "Invalid clone target list");
      return;
    }

    uint16_t targetBit = (uint16_t)1U << target;
    if ((selectedMask & targetBit) == 0)
    {
      selectedMask |= targetBit;
      targetCount++;
    }
  }

  if (targetCount == 0)
  {
    sendResponse(request, 400, source, EpeverProfile::Unknown, nullptr,
                 "Select at least one other MPPT device");
    return;
  }

  JsonDocument document;
  JsonArray results = document["results"].to<JsonArray>();
  uint8_t successCount = 0;
  EpeverProfile sourceProfile;
  uint16_t sourceValues[BatterySettingsService::ValueCount];
  uint8_t sourceStatus;

  {
    WorkerPause pause(_workerCanRun);
    sourceProfile = _detectProfile(source, false);
    sourceStatus =
        _batterySettings.read(source, sourceProfile, sourceValues);
    if (sourceStatus == ModbusMaster::ku8MBSuccess)
    {
      for (uint8_t target = 1; target <= deviceQuantity(); target++)
      {
        if ((selectedMask & ((uint16_t)1U << target)) == 0)
          continue;

        JsonObject targetResult = results.add<JsonObject>();
        targetResult["device"] = target;
        EpeverProfile targetProfile = _detectProfile(target, false);
        targetResult["profile"] = epeverProfileName(targetProfile);
        if (!BatterySettingsService::compatible(sourceProfile,
                                                targetProfile))
        {
          targetResult["ok"] = false;
          targetResult["message"] =
              "Skipped: incompatible Legacy/NC-G3 settings layout";
          continue;
        }

        uint16_t readBack[BatterySettingsService::ValueCount];
        uint8_t writeStatus;
        uint8_t readStatus;
        BatterySettingsService::VerifyResult verifyResult =
            _batterySettings.writeAndVerify(
                target, targetProfile, sourceValues, readBack,
                writeStatus, readStatus);
        targetResult["writeCode"] = writeStatus;
        targetResult["readCode"] = readStatus;
        targetResult["ok"] =
            verifyResult == BatterySettingsService::VerifyResult::Verified;

        if (verifyResult ==
            BatterySettingsService::VerifyResult::Verified)
        {
          successCount++;
          updateJson(target, targetProfile, readBack);
          targetResult["message"] = "Cloned and verified";
        }
        else if (verifyResult ==
                 BatterySettingsService::VerifyResult::ReadBackFailed)
        {
          targetResult["message"] = "Read-back failed";
        }
        else
        {
          updateJson(target, targetProfile, readBack);
          targetResult["message"] =
              "Controller rejected or changed values";
        }
        delay(100);
      }
    }
  }

  if (sourceStatus != ModbusMaster::ku8MBSuccess)
  {
    sendResponse(
        request, 502, source, sourceProfile, nullptr,
        "Could not read source settings (code " +
            String(sourceStatus) + ")");
    return;
  }

  updateJson(source, sourceProfile, sourceValues);
  _mqttTimer = 0;
  document["source"] = source;
  document["sourceProfile"] = epeverProfileName(sourceProfile);
  document["deviceQuantity"] = deviceQuantity();
  document["ok"] = successCount == targetCount;
  document["message"] =
      String(successCount) + " of " + String(targetCount) +
      " target devices cloned successfully";
  String responseBody;
  serializeJson(document, responseBody);
  request->send(200, "application/json", responseBody);
}

bool MpptSettingsRoutes::parseSetting(
    JsonObjectConst json, uint8_t index, uint16_t scale,
    uint16_t maximum, uint16_t &value)
{
  String parameterName = "e" + String(index + 1);
  JsonVariantConst setting = json[parameterName];
  if (setting.isNull())
    return false;

  String input = setting.as<String>();
  char *end = nullptr;
  double parsed = strtod(input.c_str(), &end);
  if (input.length() == 0 || end == input.c_str() || *end != '\0' ||
      !isfinite(parsed) || parsed < 0)
    return false;

  double scaled = parsed * scale;
  double rounded = round(scaled);
  if (scaled > maximum || fabs(scaled - rounded) > 0.001)
    return false;

  value = (uint16_t)rounded;
  return true;
}

void MpptSettingsRoutes::updateJson(
    uint8_t device, EpeverProfile profile, const uint16_t *values)
{
  JsonObject deviceData =
      _liveJson["EP_" + String(device)]["DeviceData"];
  if (isNcG3Profile(profile))
  {
    deviceData["BATTERY_TYPE"] =
        values[0] < 13 ? NcG3BatteryTypes[values[0]] : "Unknown";
  }
  else
  {
    deviceData["BATTERY_TYPE"] =
        values[0] < 4 ? LegacyBatteryTypes[values[0]] : "Unknown";
  }

  deviceData["BATTERY_CAPACITY"] = values[1];
  deviceData["TEMPERATURE_COMPENSATION"] =
      values[2] / (isNcG3Profile(profile) ? -100.f : 100.f);
  deviceData["HIGH_VOLT_DISCONNECT"] = values[3] / 100.f;
  deviceData["CHARGING_LIMIT_VOLTS"] = values[4] / 100.f;
  deviceData["OVER_VOLTS_RECONNECT"] = values[5] / 100.f;
  deviceData["EQUALIZATION_VOLTS"] = values[6] / 100.f;
  deviceData["BOOST_VOLTS"] = values[7] / 100.f;
  deviceData["FLOAT_VOLTS"] = values[8] / 100.f;
  deviceData["BOOST_RECONNECT_VOLTS"] = values[9] / 100.f;
  deviceData["LOW_VOLTS_RECONNECT"] = values[10] / 100.f;
  deviceData["UNDER_VOLTS_RECOVER"] = values[11] / 100.f;
  deviceData["UNDER_VOLTS_WARNING"] = values[12] / 100.f;
  deviceData["LOW_VOLTS_DISCONNECT"] = values[13] / 100.f;
  deviceData["DISCHARGING_LIMIT_VOLTS"] = values[14] / 100.f;
  normalizeJsonNumbers(_liveJson, 2);
}

void MpptSettingsRoutes::sendResponse(
    AsyncWebServerRequest *request, uint16_t statusCode, uint8_t device,
    EpeverProfile profile, const uint16_t *values, const String &message)
{
  JsonDocument document;
  document["ok"] = statusCode == 200;
  document["device"] = device;
  document["deviceQuantity"] = deviceQuantity();
  document["profile"] = epeverProfileName(profile);
  document["maxBatteryType"] = isNcG3Profile(profile) ? 12 : 3;
  document["message"] = message;
  if (values != nullptr)
  {
    JsonArray registerValues = document["values"].to<JsonArray>();
    for (uint8_t index = 0;
         index < BatterySettingsService::ValueCount; index++)
      registerValues.add(values[index]);
  }

  String body;
  serializeJson(document, body);
  request->send(statusCode, "application/json", body);
}

const char *MpptSettingsRoutes::validationMessage(
    BatterySettingsService::ValidationError error)
{
  switch (error)
  {
  case BatterySettingsService::ValidationError::ChargeVoltageOrder:
    return "Required: E4 > E5 >= E7 >= E8 >= E9 > E10";
  case BatterySettingsService::ValidationError::DischargeVoltageOrder:
    return "Required: E12 > E13 > E14 > E15";
  case BatterySettingsService::ValidationError::RecoveryVoltageOrder:
    return "Required: E4 > E6 and E11 > E14";
  default:
    return "";
  }
}
