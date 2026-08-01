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
#include "epever/DeviceAddressService.h"
#include "web/MpptSettingsRoutes.h"
#include "web/WebUiRoutes.h"
#include "web/WebSocketController.h"
#include "web/SystemActionRoutes.h"
#include "mqtt/MqttService.h"
#include "network/NetworkManager.h"
#include "app/PollingService.h"
#include "app/BootLoopGuard.h"
#include "app/NotificationLed.h"
#include "app/DiagnosticLog.h"
#include "app/ApplicationRequests.h"
#include "app/PollingControl.h"
#include "app/TemperatureSensorService.h"
#ifdef EPEVER_SIMULATION
#include "simulation/SimulationDataSource.h"
#endif
#include <time.h>
#include <coredecls.h>

bool setNTPTimeToDevice = false;
unsigned int jsonSize = 0;
char mqttClientId[ProjectConfig::MqttClientIdSize];
bool mdnsStarted = false;

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
OneWire oneWire(ProjectConfig::TemperatureSensorPin);
DallasTemperature tempSens(&oneWire);
TemperatureSensorService temperatureSensorService(tempSens);

JsonDocument liveJson;
PollingControl pollingControl;
ApplicationRequests applicationRequests;
#ifdef EPEVER_SIMULATION
SimulationDataSource simulationDataSource;
EpeverController epeverController(
    epnode, liveJson, _settings, uTime, SOFTWARE_VERSION,
    simulationDataSource);
BatterySettingsService batterySettingsService(epnode, simulationDataSource);
DeviceClockService deviceClockService(
    epnode, pollingControl, epeverController, simulationDataSource);
#else
EpeverController epeverController(
    epnode, liveJson, _settings, uTime, SOFTWARE_VERSION);
BatterySettingsService batterySettingsService(epnode);
DeviceClockService deviceClockService(
    epnode, pollingControl, epeverController);
#endif
DeviceAddressService deviceAddressService(
    EPEVER_SERIAL, ProjectConfig::ModbusTransceiverEnablePin);
MqttService mqttService(
    mqttclient, _settings, liveJson, pollingControl, epeverController,
    SOFTWARE_VERSION);
MpptSettingsRoutes mpptSettingsRoutes(
    server, _settings, batterySettingsService, epeverController,
    pollingControl, mqttService, ProjectConfig::MaximumDevices);
WebUiRoutes webUiRoutes(
    server, _settings, liveJson, deviceClockService, applicationRequests,
    ProjectConfig::MaximumDevices, SOFTWARE_VERSION);
WebSocketController webSocketController(
    ws, liveJson, pollingControl, epeverController, mqttService);
SystemActionRoutes systemActionRoutes(
    server, _settings, applicationRequests, pollingControl,
    deviceAddressService);
NetworkManager networkManager(server, dns, _settings);
PollingService pollingService(
    _settings, liveJson, epeverController, deviceClockService,
    webSocketController, mqttService, temperatureSensorService,
    setNTPTimeToDevice);
BootLoopGuard bootLoopGuard(_settings);
NotificationLed notificationLed(
    ProjectConfig::StatusLedPin, _settings, mqttclient, epeverController);

ADC_MODE(ADC_VCC);

//----------------------------------------------------------------------
void onNtpTimeSet()
{
  setNTPTimeToDevice = true;
}

void preTransmission()
{
  digitalWrite(ProjectConfig::ModbusTransceiverEnablePin, HIGH);
}

void postTransmission()
{
  digitalWrite(ProjectConfig::ModbusTransceiverEnablePin, LOW);
}

void initializeModbus()
{
  pinMode(ProjectConfig::ModbusTransceiverEnablePin, OUTPUT);
  EPEVER_SERIAL.begin(ProjectConfig::ModbusBaud);
  epnode.setResponseTimeout(ProjectConfig::ModbusResponseTimeoutMs);
  epnode.begin(1, EPEVER_SERIAL);
  epnode.preTransmission(preTransmission);
  epnode.postTransmission(postTransmission);
}

void initializeTimeSynchronization()
{
  if (strlen(_settings.data.NTPTimezone) == 0 ||
      strlen(_settings.data.NTPServer) == 0)
    return;

  configTime(_settings.data.NTPTimezone, _settings.data.NTPServer);
  settimeofday_cb(onNtpTimeSet);
}

void initializeWebServer()
{
  WiFi.hostname(_settings.data.deviceName);
  liveJson["DEVICE_NAME"] = _settings.data.deviceName;

  webUiRoutes.registerRoutes();
  mpptSettingsRoutes.registerRoutes();
  systemActionRoutes.registerRoutes();
  server.onNotFound([](AsyncWebServerRequest *request)
                    { request->send(418, "text/plain",
                                    "418 I'm a teapot"); });

  webSocketController.begin();
  server.addHandler(&ws);

  // The hardware UART is reserved exclusively for Modbus RTU.
  webSerial.setBuffer(ProjectConfig::WebSerialBufferSize);
  webSerial.begin(&server, "/webserial-data");
  DiagnosticLog::begin(webSerial);
  server.begin();
}

void updateMdns()
{
  if (!mdnsStarted && WiFi.status() == WL_CONNECTED)
  {
    mdnsStarted = MDNS.begin(_settings.data.deviceName);
    if (mdnsStarted)
      MDNS.addService("http", "tcp", 80);
  }
  if (mdnsStarted)
    MDNS.update();
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
  notificationLed.begin();
  bootLoopGuard.recordBoot();
  WiFi.persistent(true);
  initializeModbus();
  initializeTimeSynchronization();

  snprintf(mqttClientId, sizeof(mqttClientId), "%s-%06X",
           _settings.data.deviceName, ESP.getChipId());
  networkManager.connect();
  mqttService.begin(mqttClientId);
  initializeWebServer();
  updateMdns();
  DiagnosticLog::println(
      F("[APP] EPEver2MQTT " SOFTWARE_VERSION " started"));
#ifdef EPEVER_SIMULATION
  const bool simulationSelfTestPassed = simulationDataSource.selfTest();
  liveJson["SIMULATION"] = true;
  liveJson["SIMULATION_SELF_TEST"] =
      simulationSelfTestPassed ? "passed" : "failed";
  DiagnosticLog::printf("[SIM] Register self-test %s",
                        simulationSelfTestPassed ? "passed" : "failed");
#endif

  temperatureSensorService.begin();
  analogWrite(ProjectConfig::StatusLedPin, 255);
  bootLoopGuard.markBootSuccessful();
}

void loop()
{
  updateMdns();
  webSocketController.cleanup();
  if (applicationRequests.consumeFactoryReset())
  {
    _settings.reset();
    ESP.eraseConfig();
    ESP.restart();
  }
  // Make sure wifi is in the right mode
  if (WiFi.status() == WL_CONNECTED && pollingControl.canRun())
  { // No use going to next step unless WIFI is up and running.
    mqttService.loop(); // Check if we have something to read from MQTT
    pollingService.run();

    if ((applicationRequests.discoveryRequested() ||
         _settings.data.haDiscovery) &&
        measureJson(liveJson) > jsonSize)
    {
      if (mqttService.publishDiscovery())
      {
        applicationRequests.clearDiscoveryRequest();
        jsonSize = measureJson(liveJson);
      }
    }
  }

  if (applicationRequests.restartDue(
          millis(), ProjectConfig::RestartDelayMs))
  {
    DiagnosticLog::println("Restart");
    ESP.reset();
  }
  if (pollingControl.canRun())
  {
    notificationLed.update();
  }
}
