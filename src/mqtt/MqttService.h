#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

#include "../Settings.h"
#include "../app/PollingControl.h"
#include "../epever/EpeverController.h"

class MqttService
{
public:
  MqttService(PubSubClient &client, Settings &settings,
              const JsonDocument &liveJson, PollingControl &pollingControl,
              EpeverController &controller,
              const char *softwareVersion);

  void begin(const char *clientId);
  void loop();
  bool publishDue(unsigned long now) const;
  void requestPublish();
  bool publish();
  bool publishDiscovery();

private:
  bool connect();
  void onMessage(char *topic, uint8_t *payload, unsigned int length);
  void subscribeControlTopics();
  void handleControl(uint8_t device, JsonVariantConst control);
  bool publishText(const String &topic, const String &payload,
                   bool retained = false);
  bool publishDiscoveryEntity(const String &component, const String &nodeId,
                              const String &objectId, const String &payload);
  String deviceDescription(const char *deviceKey) const;

  PubSubClient &_client;
  Settings &_settings;
  const JsonDocument &_liveJson;
  PollingControl &_pollingControl;
  EpeverController &_controller;
  const char *_softwareVersion;
  char _clientId[80] = {};
  bool _publishRequested = true;
  unsigned long _lastPublishAttempt = 0;
};
