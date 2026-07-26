#pragma once

#ifndef ARDUINOJSON_USE_DOUBLE
#define ARDUINOJSON_USE_DOUBLE 0
#endif
#ifndef ARDUINOJSON_USE_LONG_LONG
#define ARDUINOJSON_USE_LONG_LONG 1
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

#include "../Settings.h"
#include "../epever/ControllerTypes.h"

class MqttService
{
public:
  using DetectProfileFn = EpeverProfile (*)(uint8_t device, bool force);
  using RatedCurrentFn = uint16_t (*)(uint8_t device);
  using WriteLoadStateFn = bool (*)(uint8_t device, bool state);
  using WriteChargeCurrentFn = bool (*)(uint8_t device, float amps);

  MqttService(PubSubClient &client, Settings &settings,
              JsonDocument &liveJson, bool &workerCanRun,
              unsigned long &publishTimer, DetectProfileFn detectProfile,
              RatedCurrentFn ratedCurrent, WriteLoadStateFn writeLoadState,
              WriteChargeCurrentFn writeChargeCurrent,
              const char *softwareVersion);

  void begin(const char *clientId);
  void loop();
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
  JsonDocument &_liveJson;
  bool &_workerCanRun;
  unsigned long &_publishTimer;
  DetectProfileFn _detectProfile;
  RatedCurrentFn _ratedCurrent;
  WriteLoadStateFn _writeLoadState;
  WriteChargeCurrentFn _writeChargeCurrent;
  const char *_softwareVersion;
  char _clientId[80] = {};
};
