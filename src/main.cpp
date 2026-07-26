#include <Arduino.h>
#include "main.h"
#include <ModbusMaster.h>

#include <PubSubClient.h>

#include <ArduinoJson.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncWebServer.h>
#include <UnixTime.h>
#include <Updater.h>
#include <MycilaWebSerial.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "Settings.h"
#include "epever/BatterySettingsService.h"
#include "epever/DeviceClockService.h"
#include "epever/EpeverController.h"
#include "web/MpptSettingsRoutes.h"
#include "web/WebUiRoutes.h"
#include "web/WebSocketController.h"
#include "web/SystemActionRoutes.h"
#include "mqtt/MqttService.h"
#include "network/NetworkManager.h"
#include "app/PollingService.h"
#include "app/BootLoopGuard.h"
#include "app/NotificationLed.h"
#ifdef EPEVER_SIMULATION
#include "simulation/SimulationDataSource.h"
#endif
#include <time.h>
#include <coredecls.h>

// flag for saving data and other things
bool shouldSaveConfig = false;
bool restartNow = false;
bool workerCanRun = true;
bool haDiscTrigger = false;
bool setNTPTimeToDevice = false;
bool factoryResetRequested = false;
unsigned int jsonSize = 0;
unsigned long mqtttimer = 0;
unsigned long RestartTimer = 0;
unsigned long notifyTimer = 0;
unsigned long slowDownTimer = 0;
byte ReqDevAddr = 1;
char mqttClientId[80];
int errorcode;
uint8_t numOfTempSens;
DeviceAddress tempDeviceAddress;

WebSerial webSerial;
WiFiClient client;
Settings _settings;
PubSubClient mqttclient(client);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dns;
ModbusMaster epnode; // instantiate ModbusMaster object
// Controller RTC values are local wall-clock values. GMT 0 creates a stable
// transport timestamp without applying a hard-coded timezone offset.
UnixTime uTime(0);
OneWire oneWire(TEMPSENS_PIN);
DallasTemperature tempSens(&oneWire);

JsonDocument liveJson;
#ifdef EPEVER_SIMULATION
SimulationDataSource simulationDataSource;
EpeverController epeverController(
    epnode, liveJson, _settings, uTime, tempSens, numOfTempSens,
    tempDeviceAddress, errorcode, SOFTWARE_VERSION, simulationDataSource);
BatterySettingsService batterySettingsService(epnode, simulationDataSource);
DeviceClockService deviceClockService(
    epnode, workerCanRun, detectEpeverProfile, simulationDataSource);
#else
EpeverController epeverController(
    epnode, liveJson, _settings, uTime, tempSens, numOfTempSens,
    tempDeviceAddress, errorcode, SOFTWARE_VERSION);
BatterySettingsService batterySettingsService(epnode);
DeviceClockService deviceClockService(
    epnode, workerCanRun, detectEpeverProfile);
#endif
MpptSettingsRoutes mpptSettingsRoutes(
    server, _settings, batterySettingsService, liveJson, workerCanRun,
    mqtttimer, MAX_DEVICES, detectEpeverProfile);
WebUiRoutes webUiRoutes(
    server, _settings, liveJson, deviceClockService, restartNow, RestartTimer,
    MAX_DEVICES, SOFTWARE_VERSION);
WebSocketController webSocketController(
    ws, liveJson, workerCanRun, mqtttimer, writeEpeverLoadState);
MqttService mqttService(
    mqttclient, _settings, liveJson, workerCanRun, mqtttimer,
    detectEpeverProfile, getEpeverRatedChargeCurrent,
    writeEpeverLoadState, writeNcG3ChargeCurrentLimit, SOFTWARE_VERSION);
SystemActionRoutes systemActionRoutes(
    server, _settings, factoryResetRequested, haDiscTrigger,
    EPEVER_SERIAL, EPEVER_DE_RE);
NetworkManager networkManager(server, dns, _settings, shouldSaveConfig);
PollingService pollingService(
    _settings, liveJson, epeverController, deviceClockService,
    webSocketController, mqttService, tempSens, setNTPTimeToDevice,
    errorcode, ReqDevAddr, mqtttimer, notifyTimer, slowDownTimer);
BootLoopGuard bootLoopGuard(_settings);
NotificationLed notificationLed(LED_PIN, _settings, mqttclient, errorcode);

ADC_MODE(ADC_VCC);

//----------------------------------------------------------------------
void NTPTimeSetCB()
{
  setNTPTimeToDevice = true;
}

void preTransmission()
{
  digitalWrite(EPEVER_DE_RE, 1);
}

void postTransmission()
{
  digitalWrite(EPEVER_DE_RE, 0);
}


void setup()
{
  _settings.load();
#ifdef EPEVER_SIMULATION
  _settings.data.deviceQuantity = SimulationDataSource::DeviceCount;
  strlcpy(_settings.data.deviceName, "EPEver2MQTT-Simulator",
          sizeof(_settings.data.deviceName));
  simulationDataSource.begin();
#endif
  pinMode(EPEVER_DE_RE, OUTPUT);
  notificationLed.begin();
  bootLoopGuard.recordBoot();
  WiFi.persistent(true);              // fix wifi save bug

  EPEVER_SERIAL.begin(EPEVER_BAUD);
  epnode.setResponseTimeout(100);
  epnode.begin(1, EPEVER_SERIAL);
  epnode.preTransmission(preTransmission);
  epnode.postTransmission(postTransmission);
  // https://werner.rothschopf.net/202011_arduino_esp8266_ntp_en.htm
  if (strlen(_settings.data.NTPTimezone) != 0 && strlen(_settings.data.NTPServer) != 0)
  {
    configTime(_settings.data.NTPTimezone, _settings.data.NTPServer);
    settimeofday_cb(NTPTimeSetCB);
  }

  sprintf(mqttClientId, "%s-%06X", _settings.data.deviceName, ESP.getChipId());
  bool res = networkManager.connect();

  mqttService.begin(mqttClientId);
  // check is WiFi connected

  if (res)
  {
    // set the device name
    MDNS.begin(_settings.data.deviceName);
    MDNS.addService("http", "tcp", 80);

    WiFi.hostname(_settings.data.deviceName);

    liveJson["DEVICE_NAME"] = _settings.data.deviceName;

    webUiRoutes.registerRoutes();
    mpptSettingsRoutes.registerRoutes();
    systemActionRoutes.registerRoutes();

    server.onNotFound([](AsyncWebServerRequest *request)
                      { request->send(418, "text/plain", "418 I'm a teapot"); });

    webSocketController.begin();
    server.addHandler(&ws);

    // WebSerial is accessible at "<IP Address>/webserial" in browser
    webSerial.begin(&server);
    // webSerial.onMessage(recvMsg);

    server.begin();

    tempSens.begin();
    numOfTempSens = tempSens.getDeviceCount();
  }
  analogWrite(LED_PIN, 255);
  bootLoopGuard.markBootSuccessful();
}

void loop()
{
  MDNS.update();
  if (factoryResetRequested)
  {
    factoryResetRequested = false;
    _settings.reset();
    ESP.eraseConfig();
    ESP.restart();
  }
  if (Update.isRunning())
  {
    workerCanRun = false;
  }
  // Make sure wifi is in the right mode
  if (WiFi.status() == WL_CONNECTED && workerCanRun)
  { // No use going to next step unless WIFI is up and running.
    // ws.cleanupClients(); // clean unused client connections

    mqttService.loop(); // Check if we have something to read from MQTT
    pollingService.run();

    if ((haDiscTrigger || _settings.data.haDiscovery) && measureJson(liveJson) > jsonSize)
    {
      if (mqttService.publishDiscovery())
      {
        haDiscTrigger = false;
        jsonSize = measureJson(liveJson);
      }
    }
  }

  if (restartNow && millis() >= (RestartTimer + 500))
  {
    Serial.println("Restart");
    ESP.reset();
  }
  if (workerCanRun)
  {
    notificationLed.update();
  }
}

EpeverProfile detectEpeverProfile(uint8_t device, bool force)
{
  return epeverController.detectProfile(device, force);
}

bool writeEpeverLoadState(uint8_t device, bool state)
{
  return epeverController.writeLoadState(device, state);
}

bool writeNcG3ChargeCurrentLimit(uint8_t device, float amps)
{
  return epeverController.writeChargeCurrentLimit(device, amps);
}

uint16_t getEpeverRatedChargeCurrent(uint8_t device)
{
  return epeverController.ratedChargeCurrent(device);
}
