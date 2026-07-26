#include "MqttService.h"

#include <ESP8266WiFi.h>
#include <StreamUtils.h>

#include "HaDiscoveryDescriptors.h"

namespace
{
constexpr uint16_t MqttBufferSize = 512;

String mqttValue(JsonVariantConst value)
{
  if (value.is<float>())
    return String(value.as<float>(), 2);
  return value.as<String>();
}
}

MqttService::MqttService(
    PubSubClient &client, Settings &settings, JsonDocument &liveJson,
    bool &workerCanRun, unsigned long &publishTimer,
    DetectProfileFn detectProfile, RatedCurrentFn ratedCurrent,
    WriteLoadStateFn writeLoadState, WriteChargeCurrentFn writeChargeCurrent,
    const char *softwareVersion)
    : _client(client),
      _settings(settings),
      _liveJson(liveJson),
      _workerCanRun(workerCanRun),
      _publishTimer(publishTimer),
      _detectProfile(detectProfile),
      _ratedCurrent(ratedCurrent),
      _writeLoadState(writeLoadState),
      _writeChargeCurrent(writeChargeCurrent),
      _softwareVersion(softwareVersion)
{
}

void MqttService::begin(const char *clientId)
{
  strlcpy(_clientId, clientId, sizeof(_clientId));
  _client.setServer(_settings.data.mqttServer, _settings.data.mqttPort);
  _client.setBufferSize(MqttBufferSize);
  _client.setCallback(
      [this](char *topic, uint8_t *payload, unsigned int length)
      { onMessage(topic, payload, length); });
}

void MqttService::loop()
{
  _client.loop();
}

bool MqttService::connect()
{
  if (_client.connected())
    return true;
  if (strlen(_settings.data.mqttServer) == 0 ||
      _settings.data.mqttPort == 0)
    return false;

  const String root = _settings.data.mqttTopic;
  if (!_client.connect(_clientId, _settings.data.mqttUser,
                       _settings.data.mqttPassword,
                       (root + "/Alive").c_str(), 0, true, "false", true))
    return false;

  _client.publish((root + "/IP").c_str(),
                  WiFi.localIP().toString().c_str(), true);
  _client.publish((root + "/Alive").c_str(), "true", true);
  subscribeControlTopics();
  return true;
}

void MqttService::subscribeControlTopics()
{
  const String root = _settings.data.mqttTopic;
  if (strlen(_settings.data.mqttTriggerPath) > 0)
    _client.subscribe(_settings.data.mqttTriggerPath);

  if (_settings.data.mqttJson)
  {
    _client.subscribe((root + "/DATA").c_str());
    return;
  }

  for (uint8_t device = 1;
       device <= _settings.data.deviceQuantity; device++)
  {
    const String prefix =
        root + "/EP_" + String(device) + "/DeviceControl/";
    _client.subscribe((prefix + "LOAD_STATE").c_str());
    _client.subscribe((prefix + "CHARGING_CURRENT_LIMIT").c_str());
  }
}

bool MqttService::publish()
{
  if (!connect())
    return false;

  const String root = _settings.data.mqttTopic;
  _client.publish((root + "/Alive").c_str(), "true", true);
  _client.publish((root + "/Wifi_RSSI").c_str(),
                  String(WiFi.RSSI()).c_str());

  if (_settings.data.mqttJson)
  {
    const String dataTopic = root + "/DATA";
    if (!_client.beginPublish(dataTopic.c_str(), measureJson(_liveJson), false))
      return false;
    BufferingPrint bufferedClient(_client, 32);
    serializeJson(_liveJson, bufferedClient);
    bufferedClient.flush();
    return _client.endPublish();
  }

  for (JsonPairConst device : _liveJson.as<JsonObjectConst>())
  {
    if (strncmp(device.key().c_str(), "EP_", 3) != 0)
      continue;
    for (JsonPairConst group : device.value().as<JsonObjectConst>())
      for (JsonPairConst value : group.value().as<JsonObjectConst>())
      {
        const String valueTopic =
            root + "/" + device.key().c_str() + "/" +
            group.key().c_str() + "/" + value.key().c_str();
        const String payload = mqttValue(value.value());
        _client.publish(valueTopic.c_str(), payload.c_str());
      }
  }

  for (JsonPairConst value : _liveJson.as<JsonObjectConst>())
  {
    if (strncmp(value.key().c_str(), "DS18B20_", 8) == 0)
    {
      const String payload = mqttValue(value.value());
      _client.publish((root + "/" + value.key().c_str()).c_str(),
                      payload.c_str());
    }
  }
  return true;
}

void MqttService::onMessage(char *receivedTopic, uint8_t *payload,
                            unsigned int length)
{
  if (_settings.data.mqttJson)
  {
    JsonDocument document;
    if (deserializeJson(document, payload, length))
      return;
    for (uint8_t device = 1;
         device <= _settings.data.deviceQuantity; device++)
    {
      JsonVariantConst control =
          document["EP_" + String(device)]["DeviceControl"];
      if (!control.isNull())
        handleControl(device, control);
    }
  }
  else
  {
    String message;
    message.reserve(length);
    for (unsigned int index = 0; index < length; index++)
      message += static_cast<char>(payload[index]);

    const String root = _settings.data.mqttTopic;
    for (uint8_t device = 1;
         device <= _settings.data.deviceQuantity; device++)
    {
      const String prefix =
          root + "/EP_" + String(device) + "/DeviceControl/";
      if (strcmp(receivedTopic, (prefix + "LOAD_STATE").c_str()) == 0)
      {
        JsonDocument control;
        if (message == "true" || message == "false")
        {
          control["LOAD_STATE"] = message == "true";
          handleControl(device, control.as<JsonVariantConst>());
        }
      }
      else if (strcmp(receivedTopic,
                      (prefix + "CHARGING_CURRENT_LIMIT").c_str()) == 0)
      {
        char *end = nullptr;
        const float amps = strtof(message.c_str(), &end);
        if (end != message.c_str() && *end == '\0')
        {
          JsonDocument control;
          control["CHARGING_CURRENT_LIMIT"] = amps;
          handleControl(device, control.as<JsonVariantConst>());
        }
      }
    }
  }

  if (strlen(_settings.data.mqttTriggerPath) > 0 &&
      strcmp(receivedTopic, _settings.data.mqttTriggerPath) == 0)
    _publishTimer = 0;
}

void MqttService::handleControl(uint8_t device,
                                JsonVariantConst control)
{
  const bool previousWorkerState = _workerCanRun;
  _workerCanRun = false;
  if (!control["LOAD_STATE"].isNull())
    _writeLoadState(device, control["LOAD_STATE"].as<bool>());
  if (!control["CHARGING_CURRENT_LIMIT"].isNull())
    _writeChargeCurrent(
        device, control["CHARGING_CURRENT_LIMIT"].as<float>());
  _workerCanRun = previousWorkerState;
  _publishTimer = 0;
}

bool MqttService::publishText(const String &topic, const String &payload,
                              bool retained)
{
  if (!_client.beginPublish(topic.c_str(), payload.length(), retained))
    return false;
  for (size_t index = 0; index < payload.length(); index++)
    _client.write(payload[index]);
  return _client.endPublish();
}

String MqttService::deviceDescription(const char *deviceKey) const
{
  return String("\"dev\":{") +
         "\"ids\":[\"" + _clientId + "_" + deviceKey + "\"]," +
         "\"name\":\"" + _settings.data.deviceName + "_" + deviceKey + "\"," +
         "\"cu\":\"http://" + WiFi.localIP().toString() + "\"," +
         "\"mdl\":\"EPEver2MQTT_" + deviceKey + "\"," +
         "\"mf\":\"SoftwareCrash\"," +
         "\"sw\":\"" + _softwareVersion + "\"}";
}

bool MqttService::publishDiscoveryEntity(
    const String &component, const String &nodeId, const String &objectId,
    const String &payload)
{
  const String discoveryTopic =
      "homeassistant/" + component + "/" + nodeId + "/" + objectId +
      "/config";
  return publishText(discoveryTopic, payload, true);
}

bool MqttService::publishDiscovery()
{
  if (!connect())
    return false;

  const String root = _settings.data.mqttTopic;
  bool success = true;
  for (JsonPairConst device : _liveJson.as<JsonObjectConst>())
  {
    if (strncmp(device.key().c_str(), "EP_", 3) != 0)
      continue;

    const char *deviceKey = device.key().c_str();
    const String nodeId = root + "_" + deviceKey;
    const String description = deviceDescription(deviceKey);
    const uint8_t deviceNumber = atoi(deviceKey + 3);
    const EpeverProfile profile =
        _detectProfile(deviceNumber, false);

    if (profile != EpeverProfile::EtNcG3)
    {
      const String payload =
          String("{\"name\":\"LOAD_STATE\",") +
          "\"command_topic\":\"" + root + "/" + deviceKey +
          "/DeviceControl/LOAD_STATE\"," +
          "\"stat_t\":\"" + root + "/" + deviceKey +
          "/LiveData/LOAD_STATE\"," +
          "\"avty_t\":\"" + root + "/Alive\"," +
          "\"pl_avail\":\"true\",\"pl_not_avail\":\"false\"," +
          "\"uniq_id\":\"" + _clientId + ".LOAD_STATE_" + deviceKey +
          "\",\"ic\":\"mdi:toggle-switch-off\"," +
          "\"pl_on\":\"true\",\"pl_off\":\"false\"," +
          "\"stat_on\":\"true\",\"stat_off\":\"false\"," +
          description + "}";
      success &= publishDiscoveryEntity("switch", nodeId, "LOAD_STATE",
                                        payload);
    }

    const uint16_t ratedCurrent = _ratedCurrent(deviceNumber);
    if (isNcG3Profile(profile) && ratedCurrent > 0)
    {
      const String payload =
          String("{\"name\":\"CHARGING_CURRENT_LIMIT\",") +
          "\"command_topic\":\"" + root + "/" + deviceKey +
          "/DeviceControl/CHARGING_CURRENT_LIMIT\"," +
          "\"stat_t\":\"" + root + "/" + deviceKey +
          "/DeviceData/CHARGING_CURRENT_LIMIT\"," +
          "\"avty_t\":\"" + root + "/Alive\"," +
          "\"pl_avail\":\"true\",\"pl_not_avail\":\"false\"," +
          "\"uniq_id\":\"" + _clientId +
          ".CHARGING_CURRENT_LIMIT_" + deviceKey + "\"," +
          "\"ic\":\"mdi:current-dc\",\"unit_of_meas\":\"A\"," +
          "\"mode\":\"box\",\"min\":0.01,\"max\":" +
          String(ratedCurrent / 100.0f, 2) +
          ",\"step\":0.01," + description + "}";
      success &= publishDiscoveryEntity(
          "number", nodeId, "CHARGING_CURRENT_LIMIT", payload);
    }

    for (JsonPairConst group : device.value().as<JsonObjectConst>())
      for (JsonPairConst value : group.value().as<JsonObjectConst>())
        for (size_t index = 0;
             index < sizeof HaDescriptors / sizeof HaDescriptors[0]; index++)
        {
          if (strcmp(value.key().c_str(), HaDescriptors[index][0]) != 0)
            continue;
          String payload =
              String("{\"name\":\"") + HaDescriptors[index][0] + "\"," +
              "\"stat_t\":\"" + root + "/" + deviceKey + "/" +
              group.key().c_str() + "/" + HaDescriptors[index][0] + "\"," +
              "\"avty_t\":\"" + root + "/Alive\"," +
              "\"pl_avail\":\"true\",\"pl_not_avail\":\"false\"," +
              "\"uniq_id\":\"" + _clientId + "." + HaDescriptors[index][0] +
              "_" + deviceKey + "\",\"ic\":\"mdi:" +
              HaDescriptors[index][1] + "\",";
          if (strlen(HaDescriptors[index][2]) > 0)
            payload += String("\"unit_of_meas\":\"") +
                       HaDescriptors[index][2] + "\",";
          if (strlen(HaDescriptors[index][3]) > 0)
            payload += String("\"dev_cla\":\"") +
                       HaDescriptors[index][3] + "\",";
          if (strcmp(HaDescriptors[index][2], "kWh") == 0)
            payload += "\"state_class\":\"total_increasing\",";
          else if (strcmp(HaDescriptors[index][2], "A") == 0 ||
                   strcmp(HaDescriptors[index][2], "V") == 0 ||
                   strcmp(HaDescriptors[index][2], "W") == 0)
            payload += "\"state_class\":\"measurement\",";
          payload += description + "}";
          success &= publishDiscoveryEntity(
              "sensor", nodeId, HaDescriptors[index][0], payload);
        }
  }
  return success;
}
