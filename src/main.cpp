#include <Arduino.h>
#include "main.h"
#include <ModbusMaster.h>
#include "epregister.h"

#include <EEPROM.h>
#include <PubSubClient.h>

#include <ArduinoJson.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncWiFiManager.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <UnixTime.h>
#include <Updater.h>
#include <MycilaWebSerial.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include <StreamUtils.h>

#include "Settings.h"      //settings functions
#include "html.h"          //the HTML content
#include "htmlProzessor.h" // The html Prozessor
#include <time.h>
#include <coredecls.h>

String topic = "";           // Default first part of topic. We will add device ID in setup
String devicePrefix = "EP_"; // prefix for datapath for every device

// flag for saving data and other things
bool shouldSaveConfig = false;
bool restartNow = false;
bool workerCanRun = true;
bool haDiscTrigger = false;
bool setNTPTimeToDevice = false;
unsigned int jsonSize = 0;
unsigned long mqtttimer = 0;
unsigned long RestartTimer = 0;
unsigned long notifyTimer = 0;
unsigned long slowDownTimer = 0;
byte ReqDevAddr = 1;
char mqtt_server[80];
char mqttClientId[80];
int errorcode;
uint8_t numOfTempSens;
DeviceAddress tempDeviceAddress;
uint32_t bootcount = 0;

WebSerial webSerial;
WiFiClient client;
Settings _settings;
PubSubClient mqttclient(client);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncWebSocketClient *wsClient;
DNSServer dns;
ModbusMaster epnode; // instantiate ModbusMaster object
UnixTime uTime(3);

JsonDocument liveJson;

OneWire oneWire(TEMPSENS_PIN);
DallasTemperature tempSens(&oneWire);

time_t timeNow;
struct tm NTPTime;

#include "status-LED.h"
ADC_MODE(ADC_VCC);

/* //remove after testing!!
uint32_t sntp_update_delay_MS_rfc_not_less_than_15000 () {
  return 900000UL;
} */

//----------------------------------------------------------------------
void NTPTimeSetCB()
{
  setNTPTimeToDevice = true;
}

void saveConfigCallback()
{
  DEBUG_WEBLN(F("Should save config"));
  shouldSaveConfig = true;
}

void preTransmission()
{
  digitalWrite(EPEVER_DE_RE, 1);
}

void postTransmission()
{
  digitalWrite(EPEVER_DE_RE, 0);
}

void notifyClients()
{
  if (wsClient != nullptr && wsClient->canSend())
  {
    size_t len = measureJson(liveJson);
    AsyncWebSocketMessageBuffer *buffer = ws.makeBuffer(len);
    if (buffer)
    {
      serializeJson(liveJson, (char *)buffer->get(), len + 1);
      wsClient->text(buffer);
    }
  }
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    data[len] = 0;
    if (String((char *)data).substring(0, 11) == "loadSwitch_") // get switch data from web loadSwitch_1_1
    {
      uint8_t device = String((char *)data).substring(11, 12).toInt();
      bool state = String((char *)data).substring(13, 14).toInt() != 0;
      workerCanRun = false;
      writeEpeverLoadState(device, state);
      workerCanRun = true;
      mqtttimer = 0;
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    wsClient = client;
    // notifyClients();
    DEBUG_WEBF("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    break;
  case WS_EVT_DISCONNECT:
    DEBUG_WEBF("WebSocket client #%u disconnected\n", client->id());
    wsClient = nullptr;
    ws.cleanupClients();
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
    break;
  case WS_EVT_PING:
    break;
  case WS_EVT_ERROR:
    wsClient = nullptr;
    ws.cleanupClients();
    break;
  }
}

bool resetCounter(bool count)
{

  if (count)
  {
    if (ESP.getResetInfoPtr()->reason == 6)
    {
      ESP.rtcUserMemoryRead(16, &bootcount, sizeof(bootcount));

      if (bootcount >= 10 && bootcount < 20)
      {
        // bootcount = 0;
        // ESP.rtcUserMemoryWrite(16, &bootcount, sizeof(bootcount));
        _settings.reset();
        ESP.eraseConfig();
        ESP.reset();
      }
      else
      {
        bootcount++;
        ESP.rtcUserMemoryWrite(16, &bootcount, sizeof(bootcount));
      }
    }
    else
    {
      bootcount = 0;
      ESP.rtcUserMemoryWrite(16, &bootcount, sizeof(bootcount));
    }
  }
  else
  {
    bootcount = 0;
    ESP.rtcUserMemoryWrite(16, &bootcount, sizeof(bootcount));
  }
  return true;
}

static bool isNcG3Profile(EpeverProfile profile)
{
  return profile == EpeverProfile::ItNcG3 || profile == EpeverProfile::EtNcG3;
}

static const char *epeverProfileName(EpeverProfile profile)
{
  switch (profile)
  {
  case EpeverProfile::Legacy:
    return "Legacy";
  case EpeverProfile::ItNcG3:
    return "IT-NC G3";
  case EpeverProfile::EtNcG3:
    return "ET-NC G3";
  default:
    return "Unknown";
  }
}

static uint8_t mpptDeviceQuantity()
{
  return _settings.data.deviceQuantity > MAX_DEVICES
             ? MAX_DEVICES
             : _settings.data.deviceQuantity;
}

static bool validMpptDevice(uint8_t device)
{
  return device >= 1 && device <= mpptDeviceQuantity();
}

static uint8_t readHoldingSettingsBlock(uint16_t address, uint8_t count, uint16_t *values)
{
  epnode.clearResponseBuffer();
  uint8_t status = epnode.readHoldingRegisters(address, count);
  if (status == epnode.ku8MBSuccess)
  {
    for (uint8_t index = 0; index < count; index++)
      values[index] = epnode.getResponseBuffer(index);
  }
  return status;
}

static uint8_t writeHoldingSettingsBlock(uint16_t address, uint8_t count, const uint16_t *values)
{
  epnode.clearTransmitBuffer();
  for (uint8_t index = 0; index < count; index++)
    epnode.setTransmitBuffer(index, values[index]);
  return epnode.writeMultipleRegisters(address, count);
}

static uint8_t readMpptSettings(uint8_t device, uint16_t *values)
{
  static_assert(DEVICE_SETTINGS_CNT == 15, "Web settings mapping requires 15 logical values");
  epnode.setSlaveId(device);
  EpeverProfile profile = detectEpeverProfile(device);
  if (profile == EpeverProfile::Unknown)
    return epnode.ku8MBInvalidSlaveID;

  if (!isNcG3Profile(profile))
    return readHoldingSettingsBlock(DEVICE_SETTINGS, DEVICE_SETTINGS_CNT, values);

  // NC G3 keeps the first three logical settings at 0x9000..0x9002 and
  // moves the twelve voltage thresholds to 0x9007..0x9012.
  uint8_t status = readHoldingSettingsBlock(0x9000, 3, values);
  if (status != epnode.ku8MBSuccess)
    return status;
  return readHoldingSettingsBlock(0x9007, 12, values + 3);
}

static bool mpptSettingsMatch(const uint16_t *expected, const uint16_t *actual)
{
  for (uint8_t index = 0; index < DEVICE_SETTINGS_CNT; index++)
  {
    if (expected[index] != actual[index])
      return false;
  }
  return true;
}

static uint8_t writeMpptSettings(uint8_t device, const uint16_t *values)
{
  epnode.setSlaveId(device);
  EpeverProfile profile = detectEpeverProfile(device);
  if (profile == EpeverProfile::Unknown)
    return epnode.ku8MBInvalidSlaveID;

  if (!isNcG3Profile(profile))
    return writeHoldingSettingsBlock(DEVICE_SETTINGS, DEVICE_SETTINGS_CNT, values);

  // Write the safety-relevant voltage block first. Never write the NC G3 gap
  // at 0x9003..0x9006 as part of a legacy-style contiguous transfer.
  uint8_t status = writeHoldingSettingsBlock(0x9007, 12, values + 3);
  if (status != epnode.ku8MBSuccess)
    return status;
  return writeHoldingSettingsBlock(0x9000, 3, values);
}

// 0 = verified, 1 = read-back failed, 2 = controller changed/rejected values.
static uint8_t writeAndVerifyMpptSettings(uint8_t device, const uint16_t *values, uint16_t *readBack,
                                          uint8_t &writeStatus, uint8_t &readStatus)
{
  uint16_t previousTimeout = epnode.getResponseTimeout();
  epnode.setResponseTimeout(500);
  writeStatus = writeMpptSettings(device, values);
  delay(200);
  readStatus = readMpptSettings(device, readBack);

  if (readStatus == epnode.ku8MBSuccess && mpptSettingsMatch(values, readBack))
  {
    epnode.setResponseTimeout(previousTimeout);
    return 0;
  }

  if (writeStatus == epnode.ku8MBResponseTimedOut || writeStatus == epnode.ku8MBInvalidCRC)
  {
    delay(250);
    writeStatus = writeMpptSettings(device, values);
    delay(200);
    readStatus = readMpptSettings(device, readBack);
  }

  epnode.setResponseTimeout(previousTimeout);
  if (readStatus != epnode.ku8MBSuccess)
    return 1;
  return mpptSettingsMatch(values, readBack) ? 0 : 2;
}

static void updateMpptSettingsJson(uint8_t device, EpeverProfile profile, const uint16_t *values)
{
  JsonObject deviceData = liveJson["EP_" + String(device)]["DeviceData"];
  if (isNcG3Profile(profile))
    deviceData["BATTERY_TYPE"] = values[0] < 13 ? nc_g3_battery_types[values[0]] : "Unknown";
  else
    deviceData["BATTERY_TYPE"] =
        values[0] < (sizeof batt_type / sizeof batt_type[0]) ? batt_type[values[0]] : "Unknown";
  deviceData["BATTERY_CAPACITY"] = values[1];
  deviceData["TEMPERATURE_COMPENSATION"] = values[2] / (isNcG3Profile(profile) ? -100.f : 100.f);
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
}

static void sendMpptSettingsResponse(AsyncWebServerRequest *request, uint16_t statusCode, uint8_t device,
                                     EpeverProfile profile, const uint16_t *values, const String &message)
{
  JsonDocument document;
  document["ok"] = statusCode == 200;
  document["device"] = device;
  document["deviceQuantity"] = mpptDeviceQuantity();
  document["profile"] = epeverProfileName(profile);
  document["maxBatteryType"] = isNcG3Profile(profile) ? 12 : 3;
  document["message"] = message;
  if (values != nullptr)
  {
    JsonArray registerValues = document["values"].to<JsonArray>();
    for (uint8_t index = 0; index < DEVICE_SETTINGS_CNT; index++)
      registerValues.add(values[index]);
  }

  String body;
  serializeJson(document, body);
  request->send(statusCode, "application/json", body);
}

static bool parseMpptSetting(AsyncWebServerRequest *request, uint8_t index, uint16_t scale,
                             uint16_t maximum, uint16_t &value)
{
  String parameterName = "e" + String(index + 1);
  if (!request->hasParam(parameterName, true))
    return false;

  String input = request->getParam(parameterName, true)->value();
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

static bool compatibleMpptSettingsProfiles(EpeverProfile source, EpeverProfile target)
{
  return (source == EpeverProfile::Legacy && target == EpeverProfile::Legacy) ||
         (isNcG3Profile(source) && isNcG3Profile(target));
}

void setup()
{
  _settings.load();
  pinMode(EPEVER_DE_RE, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  analogWrite(LED_PIN, 255 - _settings.data.LEDBrightness);
  resetCounter(true);
  WiFi.persistent(true);              // fix wifi save bug
  AsyncWiFiManager wm(&server, &dns); // create wifimanager instance

  EPEVER_SERIAL.begin(EPEVER_BAUD);
  epnode.setResponseTimeout(100);
  epnode.begin(1, EPEVER_SERIAL);
  epnode.preTransmission(preTransmission);
  epnode.postTransmission(postTransmission);

  wm.setSaveConfigCallback(saveConfigCallback);

  // https://werner.rothschopf.net/202011_arduino_esp8266_ntp_en.htm
  if (strlen(_settings.data.NTPTimezone) != 0 && strlen(_settings.data.NTPServer) != 0)
  {
    configTime(_settings.data.NTPTimezone, _settings.data.NTPServer);
    settimeofday_cb(NTPTimeSetCB);
  }

  sprintf(mqttClientId, "%s-%06X", _settings.data.deviceName, ESP.getChipId());

  AsyncWiFiManagerParameter custom_mqtt_server("mqtt_server", "MQTT server", NULL, 40);
  AsyncWiFiManagerParameter custom_mqtt_user("mqtt_user", "MQTT User", NULL, 32);
  AsyncWiFiManagerParameter custom_mqtt_pass("mqtt_pass", "MQTT Password", NULL, 32);
  AsyncWiFiManagerParameter custom_mqtt_topic("mqtt_topic", "MQTT Topic", "EPEver", 32);
  AsyncWiFiManagerParameter custom_mqtt_port("mqtt_port", "MQTT Port", NULL, 6);
  AsyncWiFiManagerParameter custom_mqtt_refresh("mqtt_refresh", "MQTT Send Interval", "300", 4);
  AsyncWiFiManagerParameter custom_mqtt_triggerpath("mqtt_triggerpath", "MQTT Data Trigger Path", NULL, 80);
  AsyncWiFiManagerParameter custom_device_name("device_name", "Device Name", "EPEver2MQTT", 32);
  AsyncWiFiManagerParameter custom_device_quantity("device_quantity", "Device Quantity", "1", 2);
  AsyncWiFiManagerParameter custom_static_ip("static_ip", "Static IP (empty for DHCP)", _settings.data.staticIP, 16);
  AsyncWiFiManagerParameter custom_static_gw("static_gw", "Static Gateway (empty for DHCP)", _settings.data.staticGW, 16);
  AsyncWiFiManagerParameter custom_static_sn("static_sn", "Static Subnet (empty for DHCP)", _settings.data.staticSN, 16);
  AsyncWiFiManagerParameter custom_static_dns("static_dns", "Static DNS (empty for DHCP)", _settings.data.staticDNS, 16);

  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);
  wm.addParameter(&custom_mqtt_topic);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_refresh);
  wm.addParameter(&custom_mqtt_triggerpath);
  wm.addParameter(&custom_device_name);
  wm.addParameter(&custom_device_quantity);
  wm.addParameter(&custom_static_ip);
  wm.addParameter(&custom_static_gw);
  wm.addParameter(&custom_static_sn);
  wm.addParameter(&custom_static_dns);

  wm.setDebugOutput(false);       // disable wifimanager debug output
  wm.setMinimumSignalQuality(25); // filter weak wifi signals
  wm.setConnectTimeout(10);       // how long to try to connect for before continuing
  wm.setConfigPortalTimeout(300); // auto close configportal after n seconds
  wm.setSaveConfigCallback(saveConfigCallback);

  IPAddress ip, gw, sn, dns;
  if (ip.fromString(_settings.data.staticIP) && gw.fromString(_settings.data.staticGW) && sn.fromString(_settings.data.staticSN))
  {
    dns.fromString(_settings.data.staticDNS);
    wm.setSTAStaticIPConfig(ip, gw, sn, dns);
  }

  bool res = wm.autoConnect("EPEver2MQTT-AP");

  // save settings if wifi setup is fire up
  if (shouldSaveConfig)
  {
    strncpy(_settings.data.mqttServer, custom_mqtt_server.getValue(), 40);
    strncpy(_settings.data.mqttUser, custom_mqtt_user.getValue(), 40);
    strncpy(_settings.data.mqttPassword, custom_mqtt_pass.getValue(), 40);
    _settings.data.mqttPort = atoi(custom_mqtt_port.getValue());
    strncpy(_settings.data.deviceName, custom_device_name.getValue(), 40);
    strncpy(_settings.data.mqttTopic, custom_mqtt_topic.getValue(), 40);
    _settings.data.mqttRefresh = atoi(custom_mqtt_refresh.getValue());
    strncpy(_settings.data.mqttTriggerPath, custom_mqtt_triggerpath.getValue(), 80);
    _settings.data.deviceQuantity = atoi(custom_device_quantity.getValue()) <= 0 ? 1 : atoi(custom_device_quantity.getValue());
    strncpy(_settings.data.staticIP, custom_static_ip.getValue(), 16);
    strncpy(_settings.data.staticGW, custom_static_gw.getValue(), 16);
    strncpy(_settings.data.staticSN, custom_static_sn.getValue(), 16);
    strncpy(_settings.data.staticDNS, custom_static_dns.getValue(), 16);

    _settings.save();
    ESP.restart();
  }

  topic = _settings.data.mqttTopic;
  mqttclient.setServer(_settings.data.mqttServer, _settings.data.mqttPort);
  mqttclient.setCallback(callback);
  mqttclient.setBufferSize(MQTT_BUFFER);
  // check is WiFi connected

  if (res)
  {
    // set the device name
    MDNS.begin(_settings.data.deviceName);
    MDNS.addService("http", "tcp", 80);

    WiFi.hostname(_settings.data.deviceName);

    liveJson["DEVICE_NAME"] = _settings.data.deviceName;

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              {
      if(strlen(_settings.data.httpUser) > 0 && !request->authenticate(_settings.data.httpUser, _settings.data.httpPass)) return request->requestAuthentication();
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", HTML_MAIN, htmlProcessor);
      request->send(response); });

    server.on("/livejson", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if(strlen(_settings.data.httpUser) > 0 && !request->authenticate(_settings.data.httpUser, _settings.data.httpPass)) return request->requestAuthentication();
                AsyncResponseStream *response = request->beginResponseStream("application/json");
                serializeJson(liveJson, *response);
                request->send(response); });

    server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if(strlen(_settings.data.httpUser) > 0 && !request->authenticate(_settings.data.httpUser, _settings.data.httpPass)) return request->requestAuthentication();
                AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", HTML_REBOOT, htmlProcessor);
                request->send(response);
                restartNow = true;
                RestartTimer = millis(); });

    server.on("/confirmreset", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if(strlen(_settings.data.httpUser) > 0 && !request->authenticate(_settings.data.httpUser, _settings.data.httpPass)) return request->requestAuthentication();
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", HTML_CONFIRM_RESET, htmlProcessor);
      request->send(response); });

    server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if(strlen(_settings.data.httpUser) > 0 && !request->authenticate(_settings.data.httpUser, _settings.data.httpPass)) return request->requestAuthentication();
                AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Device is Erasing...");
                response->addHeader("Refresh", "15; url=/");
                response->addHeader("Connection", "close");
                request->send(response);
                delay(500);
                _settings.reset();
                ESP.eraseConfig();
                ESP.restart(); });

    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if(strlen(_settings.data.httpUser) > 0 && !request->authenticate(_settings.data.httpUser, _settings.data.httpPass)) return request->requestAuthentication();
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", HTML_SETTINGS, htmlProcessor);
      request->send(response); });

    server.on("/settingsedit", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if(strlen(_settings.data.httpUser) > 0 && !request->authenticate(_settings.data.httpUser, _settings.data.httpPass)) return request->requestAuthentication();
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", HTML_SETTINGS_EDIT, htmlProcessor);
      request->send(response); });

    server.on("/mpptsettings", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if(strlen(_settings.data.httpUser) > 0 && !request->authenticate(_settings.data.httpUser, _settings.data.httpPass)) return request->requestAuthentication();
                AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", HTML_MPPT_SETTINGS, htmlProcessor);
                request->send(response); });

    server.on("/mpptsettingsjson", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if(strlen(_settings.data.httpUser) > 0 && !request->authenticate(_settings.data.httpUser, _settings.data.httpPass)) return request->requestAuthentication();
                uint8_t device = request->hasParam("device") ? request->getParam("device")->value().toInt() : 1;
                if (!validMpptDevice(device))
                {
                  sendMpptSettingsResponse(request, 400, device, EpeverProfile::Unknown, nullptr,
                                           "Invalid MPPT device number");
                  return;
                }

                uint16_t values[DEVICE_SETTINGS_CNT];
                workerCanRun = false;
                EpeverProfile profile = detectEpeverProfile(device);
                uint8_t status = readMpptSettings(device, values);
                workerCanRun = true;
                if (status != epnode.ku8MBSuccess)
                {
                  sendMpptSettingsResponse(request, 502, device, profile, nullptr,
                                           "Modbus read failed (code " + String(status) + ")");
                  return;
                }

                updateMpptSettingsJson(device, profile, values);
                sendMpptSettingsResponse(request, 200, device, profile, values,
                                         "Settings read successfully");
              });

    server.on("/mpptsettingsapply", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                if(strlen(_settings.data.httpUser) > 0 && !request->authenticate(_settings.data.httpUser, _settings.data.httpPass)) return request->requestAuthentication();
                uint8_t device = request->hasParam("device", true) ? request->getParam("device", true)->value().toInt() : 0;
                if (!validMpptDevice(device))
                {
                  sendMpptSettingsResponse(request, 400, device, EpeverProfile::Unknown, nullptr,
                                           "Invalid MPPT device number");
                  return;
                }

                workerCanRun = false;
                EpeverProfile profile = detectEpeverProfile(device);
                workerCanRun = true;
                if (profile == EpeverProfile::Unknown)
                {
                  sendMpptSettingsResponse(request, 502, device, profile, nullptr,
                                           "Unable to detect the controller profile");
                  return;
                }

                uint16_t values[DEVICE_SETTINGS_CNT];
                uint16_t maximumBatteryType = isNcG3Profile(profile) ? 12 : 3;
                if (!parseMpptSetting(request, 0, 1, maximumBatteryType, values[0]) ||
                    !parseMpptSetting(request, 1, 1, UINT16_MAX, values[1]) ||
                    !parseMpptSetting(request, 2, 100, 900, values[2]))
                {
                  sendMpptSettingsResponse(request, 400, device, profile, nullptr,
                                           "Battery type, capacity, or temperature compensation is invalid");
                  return;
                }
                for (uint8_t index = 3; index < DEVICE_SETTINGS_CNT; index++)
                {
                  if (!parseMpptSetting(request, index, 100, UINT16_MAX, values[index]))
                  {
                    sendMpptSettingsResponse(request, 400, device, profile, nullptr,
                                             "E" + String(index + 1) + " contains an invalid value");
                    return;
                  }
                }

                // Permit equal charge-stage voltages (common for lithium
                // profiles), while retaining the controller's safety ordering.
                if (!(values[3] > values[4] && values[4] >= values[6] &&
                      values[6] >= values[7] && values[7] >= values[8] &&
                      values[8] > values[9]))
                {
                  sendMpptSettingsResponse(request, 400, device, profile, nullptr,
                                           "Required: E4 > E5 >= E7 >= E8 >= E9 > E10");
                  return;
                }
                if (!(values[11] > values[12] && values[12] > values[13] &&
                      values[13] > values[14]))
                {
                  sendMpptSettingsResponse(request, 400, device, profile, nullptr,
                                           "Required: E12 > E13 > E14 > E15");
                  return;
                }
                if (values[3] <= values[5] || values[10] <= values[13])
                {
                  sendMpptSettingsResponse(request, 400, device, profile, nullptr,
                                           "Required: E4 > E6 and E11 > E14");
                  return;
                }

                workerCanRun = false;
                uint16_t readBack[DEVICE_SETTINGS_CNT];
                uint8_t writeStatus;
                uint8_t readStatus;
                uint8_t verifyStatus =
                    writeAndVerifyMpptSettings(device, values, readBack, writeStatus, readStatus);
                workerCanRun = true;
                if (verifyStatus == 1)
                {
                  sendMpptSettingsResponse(request, 502, device, profile, nullptr,
                                           "Write code " + String(writeStatus) +
                                               "; read-back failed with code " + String(readStatus));
                  return;
                }

                updateMpptSettingsJson(device, profile, readBack);
                if (verifyStatus == 2)
                {
                  sendMpptSettingsResponse(request, 409, device, profile, readBack,
                                           "Controller rejected or changed values; actual values were reloaded");
                  return;
                }

                mqtttimer = 0;
                sendMpptSettingsResponse(
                    request, 200, device, profile, readBack,
                    writeStatus == epnode.ku8MBSuccess
                        ? "MPPT settings applied and verified"
                        : "Settings verified despite write acknowledgement code " + String(writeStatus));
              });

    server.on("/mpptsettingsclone", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                if(strlen(_settings.data.httpUser) > 0 && !request->authenticate(_settings.data.httpUser, _settings.data.httpPass)) return request->requestAuthentication();
                uint8_t source = request->hasParam("source", true) ? request->getParam("source", true)->value().toInt() : 0;
                String targetList = request->hasParam("targets", true) ? request->getParam("targets", true)->value() : "";
                if (!validMpptDevice(source))
                {
                  sendMpptSettingsResponse(request, 400, source, EpeverProfile::Unknown, nullptr,
                                           "Invalid source MPPT device number");
                  return;
                }

                bool selected[MAX_DEVICES + 1] = {};
                uint8_t targetCount = 0;
                unsigned int start = 0;
                while (start < targetList.length())
                {
                  int comma = targetList.indexOf(',', start);
                  String token = comma < 0 ? targetList.substring(start) : targetList.substring(start, comma);
                  token.trim();
                  uint8_t target = token.toInt();
                  if (token.length() == 0 || !validMpptDevice(target) || target == source)
                  {
                    sendMpptSettingsResponse(request, 400, source, EpeverProfile::Unknown, nullptr,
                                             "Invalid clone target list");
                    return;
                  }
                  if (!selected[target])
                  {
                    selected[target] = true;
                    targetCount++;
                  }
                  if (comma < 0)
                    break;
                  start = comma + 1;
                }
                if (targetCount == 0)
                {
                  sendMpptSettingsResponse(request, 400, source, EpeverProfile::Unknown, nullptr,
                                           "Select at least one other MPPT device");
                  return;
                }

                workerCanRun = false;
                EpeverProfile sourceProfile = detectEpeverProfile(source);
                uint16_t sourceValues[DEVICE_SETTINGS_CNT];
                uint8_t sourceStatus = readMpptSettings(source, sourceValues);
                if (sourceStatus != epnode.ku8MBSuccess)
                {
                  workerCanRun = true;
                  sendMpptSettingsResponse(request, 502, source, sourceProfile, nullptr,
                                           "Could not read source settings (code " + String(sourceStatus) + ")");
                  return;
                }
                updateMpptSettingsJson(source, sourceProfile, sourceValues);

                JsonDocument document;
                document["source"] = source;
                document["sourceProfile"] = epeverProfileName(sourceProfile);
                document["deviceQuantity"] = mpptDeviceQuantity();
                JsonArray results = document["results"].to<JsonArray>();
                uint8_t successCount = 0;

                for (uint8_t target = 1; target <= mpptDeviceQuantity(); target++)
                {
                  if (!selected[target])
                    continue;

                  JsonObject targetResult = results.add<JsonObject>();
                  targetResult["device"] = target;
                  EpeverProfile targetProfile = detectEpeverProfile(target);
                  targetResult["profile"] = epeverProfileName(targetProfile);
                  if (!compatibleMpptSettingsProfiles(sourceProfile, targetProfile))
                  {
                    targetResult["ok"] = false;
                    targetResult["message"] = "Skipped: incompatible Legacy/NC-G3 settings layout";
                    continue;
                  }

                  uint16_t readBack[DEVICE_SETTINGS_CNT];
                  uint8_t writeStatus;
                  uint8_t readStatus;
                  uint8_t verifyStatus =
                      writeAndVerifyMpptSettings(target, sourceValues, readBack, writeStatus, readStatus);
                  targetResult["writeCode"] = writeStatus;
                  targetResult["readCode"] = readStatus;
                  targetResult["ok"] = verifyStatus == 0;
                  if (verifyStatus == 0)
                  {
                    successCount++;
                    updateMpptSettingsJson(target, targetProfile, readBack);
                    targetResult["message"] = "Cloned and verified";
                  }
                  else if (verifyStatus == 1)
                  {
                    targetResult["message"] = "Read-back failed";
                  }
                  else
                  {
                    updateMpptSettingsJson(target, targetProfile, readBack);
                    targetResult["message"] = "Controller rejected or changed values";
                  }
                  delay(100);
                }

                workerCanRun = true;
                mqtttimer = 0;
                document["ok"] = successCount == targetCount;
                document["message"] = String(successCount) + " of " + String(targetCount) +
                                      " target devices cloned successfully";
                String body;
                serializeJson(document, body);
                request->send(200, "application/json", body);
              });

    server.on("/settingssave", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                if(strlen(_settings.data.httpUser) > 0 && !request->authenticate(_settings.data.httpUser, _settings.data.httpPass)) return request->requestAuthentication();
                strncpy(_settings.data.mqttServer, request->arg("post_mqttServer").c_str(), 40);
                _settings.data.mqttPort = request->arg("post_mqttPort").toInt();
                strncpy(_settings.data.mqttUser, request->arg("post_mqttUser").c_str(), 40);
                strncpy(_settings.data.mqttPassword, request->arg("post_mqttPassword").c_str(), 40);
                strncpy(_settings.data.mqttTopic, request->arg("post_mqttTopic").c_str(), 40);
                _settings.data.mqttRefresh = request->arg("post_mqttRefresh").toInt() < 1 ? 1 : request->arg("post_mqttRefresh").toInt(); // prevent lower numbers
                strncpy(_settings.data.deviceName, request->arg("post_deviceName").c_str(), 40);
                strncpy(_settings.data.staticIP, request->arg("post_staticIP").c_str(), 16);
                strncpy(_settings.data.staticGW, request->arg("post_staticGW").c_str(), 16);
                strncpy(_settings.data.staticSN, request->arg("post_staticSN").c_str(), 16);
                strncpy(_settings.data.staticDNS, request->arg("post_staticDNS").c_str(), 16);
                 _settings.data.deviceQuantity = request->arg("post_deviceQuanttity").toInt() <= 0 ? 1 : request->arg("post_deviceQuanttity").toInt();
                _settings.data.mqttJson = (request->arg("post_mqttjson") == "true") ? true : false;
                strncpy(_settings.data.mqttTriggerPath, request->arg("post_mqtttrigger").c_str(), 80);
                _settings.data.webUIdarkmode = (request->arg("post_webuicolormode") == "true") ? true : false;
                strncpy(_settings.data.httpUser, request->arg("post_httpUser").c_str(), 40);
                strncpy(_settings.data.httpPass, request->arg("post_httpPass").c_str(), 40);
                _settings.data.haDiscovery = (request->arg("post_hadiscovery") == "true") ? true : false;
                strncpy(_settings.data.NTPTimezone, request->arg("post_ntptimezone").c_str(), 40);
                strncpy(_settings.data.NTPServer, request->arg("post_ntptimeserv").c_str(), 40);
                _settings.data.LEDBrightness = request->arg("post_led").toInt();
                _settings.save();
                request->redirect("/reboot"); });

    server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if(strlen(_settings.data.httpUser) > 0 && !request->authenticate(_settings.data.httpUser, _settings.data.httpPass)) return request->requestAuthentication();
          String message;
          String resultMsg = "message received";
    if (request->hasParam("datetime")) {
      message = request->getParam("datetime")->value();
        uint8_t rtcSetY  = atoi (message.substring(0, 2).c_str ());
        uint8_t rtcSetM  = atoi (message.substring(2, 4).c_str ());
        uint8_t rtcSetD  = atoi (message.substring(4, 6).c_str ());
        uint8_t rtcSeth  = atoi (message.substring(6, 8).c_str ());
        uint8_t rtcSetm  = atoi (message.substring(8, 10).c_str ());
        uint8_t rtcSets  = atoi (message.substring(10, 12).c_str ());

        for (size_t i = 1; i <= ((size_t)_settings.data.deviceQuantity); i++)
        {
          epnode.setSlaveId(i);
          EpeverProfile profile = detectEpeverProfile(i);
          if (profile == EpeverProfile::Unknown)
            continue;
          epnode.setTransmitBuffer(0, ((uint16_t)rtcSetm << 8) | rtcSets); // minute | secund
          epnode.setTransmitBuffer(1, ((uint16_t)rtcSetD << 8) | rtcSeth); // day | hour
          epnode.setTransmitBuffer(2, ((uint16_t)rtcSetY << 8) | rtcSetM); // year | month
          uint16_t clockRegister =
              (profile == EpeverProfile::ItNcG3 || profile == EpeverProfile::EtNcG3)
                  ? NC_G3_RTC_CLOCK
                  : RTC_CLOCK;
          epnode.writeMultipleRegisters(clockRegister, 3);
          delay(50);
        }
    }
     if (request->hasParam("devid")) {
      message = request->getParam("devid")->value();
        digitalWrite(EPEVER_DE_RE, 1);
          delay(50);

          byte u8TransmitRaw[8];
          u8TransmitRaw[0] = 0xF8;
          u8TransmitRaw[1] = 0x45;
          u8TransmitRaw[2] = 0x00;
          u8TransmitRaw[3] = 0x01;
          u8TransmitRaw[4] = 0x01;
          u8TransmitRaw[5] = message.toInt();

        uint16_t crcBuff = 0xFFFF;
          for (i = 0; i < 6; i++)
          {
            crcBuff = crc16_update(crcBuff, u8TransmitRaw[i]);
          }
          u8TransmitRaw[6] = lowByte(crcBuff);
          u8TransmitRaw[7] = highByte(crcBuff);
          EPEVER_SERIAL.write(u8TransmitRaw, sizeof(u8TransmitRaw));
          //read the answer
          delay(10);
          digitalWrite(EPEVER_DE_RE, 0);
          char result[4];
          EPEVER_SERIAL.readBytes(result, 4);

          if(result[2] == message.toInt()){
            resultMsg = "ID " + String(result[2], HEX) + " Successfull Set";
          } else {
            resultMsg = "ID set Fail... Actual id is: " + String(result[2], HEX);
          }
    }     
      if (request->hasParam("ha")) {
      haDiscTrigger = true;
    }      
     request->send(200, "text/plain", resultMsg.c_str()); });

    server.on(
        "/update", HTTP_POST, [](AsyncWebServerRequest *request)
        {
          if(strlen(_settings.data.httpUser) > 0 && !request->authenticate(_settings.data.httpUser, _settings.data.httpPass)) return request->requestAuthentication();
    //https://gist.github.com/JMishou/60cb762047b735685e8a09cd2eb42a60
    // the request handler is triggered after the upload has finished... 
    // create the response, add header, and send response
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", (Update.hasError())?"FAIL":"OK");
    response->addHeader("Connection", "close");
    response->addHeader("Access-Control-Allow-Origin", "*");
    //restartNow = true; // Tell the main loop to restart the ESP
    //RestartTimer = millis();  // Tell the main loop to restart the ESP
    request->send(response); },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
        {
          // Upload handler chunks in data

          if (!index)
          { // if index == 0 then this is the first frame of data
            Serial.printf("UploadStart: %s\n", filename.c_str());
            Serial.setDebugOutput(true);

            // calculate sketch space required for the update
            uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            if (!Update.begin(maxSketchSpace))
            { // start with max available size
              Update.printError(Serial);
            }
            Update.runAsync(true); // tell the updaterClass to run in async mode
          }

          // Write chunked data to the free sketch space
          if (Update.write(data, len) != len)
          {
            Update.printError(Serial);
          }

          if (final)
          { // if the final flag is set then this is the last frame of data
            if (Update.end(true))
            { // true to set the size to the current progress
              Serial.printf("Update Success: %u B\nRebooting...\n", index + len);
            }
            else
            {
              Update.printError(Serial);
            }
            Serial.setDebugOutput(false);
          }
        });

    server.onNotFound([](AsyncWebServerRequest *request)
                      { request->send(418, "text/plain", "418 I'm a teapot"); });

    ws.onEvent(onEvent);
    server.addHandler(&ws);

    // WebSerial is accessible at "<IP Address>/webserial" in browser
    webSerial.begin(&server);
    // webSerial.onMessage(recvMsg);

    server.begin();

    tempSens.begin();
    numOfTempSens = tempSens.getDeviceCount();
  }
  analogWrite(LED_PIN, 255);
  resetCounter(false);
}

void loop()
{
  MDNS.update();
  if (Update.isRunning())
  {
    workerCanRun = false;
  }
  // Make sure wifi is in the right mode
  if (WiFi.status() == WL_CONNECTED && workerCanRun)
  { // No use going to next step unless WIFI is up and running.
    // ws.cleanupClients(); // clean unused client connections

    mqttclient.loop(); // Check if we have something to read from MQTT
    epWorker();        // the loop worker

    if ((haDiscTrigger || _settings.data.haDiscovery) && measureJson(liveJson) > jsonSize)
    {
      if (sendHaDiscovery())
      {
        haDiscTrigger = false;
        jsonSize = measureJson(liveJson);
      }
    }
  }

  if (restartNow && millis() >= (RestartTimer + 500))
  {
    DEBUG_WEBLN("Restart");
    ESP.reset();
  }
  if (workerCanRun)
  {
    notificationLED(); // notification LED routine
  }
}

bool epWorker()
{
  if (millis() < (slowDownTimer + 500))
  {
    return true;
  }
  liveJson["Wifi_RSSI"] = WiFi.RSSI();

  if (strlen(_settings.data.NTPTimezone) != 0 && setNTPTimeToDevice == true)
  {
    time(&timeNow);
    localtime_r(&timeNow, &NTPTime);
    for (size_t i = 1; i <= ((size_t)_settings.data.deviceQuantity); i++)
    {
      epnode.setSlaveId(i);
      EpeverProfile profile = detectEpeverProfile(i);
      if (profile == EpeverProfile::Unknown)
      {
        DEBUG_WEBLN("[" + String(i) + "] Device profile unknown; clock write skipped");
        continue;
      }
      epnode.setTransmitBuffer(0, ((uint16_t)NTPTime.tm_min << 8) | NTPTime.tm_sec);                // minute | secund
      epnode.setTransmitBuffer(1, ((uint16_t)NTPTime.tm_mday << 8) | NTPTime.tm_hour);              // day | hour
      epnode.setTransmitBuffer(2, ((uint16_t)(NTPTime.tm_year - 100) << 8) | (NTPTime.tm_mon + 1)); // year | month
      uint16_t clockRegister =
          (profile == EpeverProfile::ItNcG3 || profile == EpeverProfile::EtNcG3)
              ? NC_G3_RTC_CLOCK
              : RTC_CLOCK;
      epnode.writeMultipleRegisters(clockRegister, 3);
      delay(50);
    }
    DEBUG_WEBLN((String)NTPTime.tm_mday + "." + (NTPTime.tm_mon + 1) + "." + (NTPTime.tm_year + 1900) + " " + NTPTime.tm_hour + ":" + NTPTime.tm_min + ":" + NTPTime.tm_sec);
    setNTPTimeToDevice = false;
  }
  /*   //for testing
    time(&timeNow);
    localtime_r(&timeNow, &NTPTime);
    DEBUG_WEBLN((String) NTPTime.tm_mday+"."+(NTPTime.tm_mon + 1)+"."+(NTPTime.tm_year + 1900 )+" "+NTPTime.tm_hour+":"+NTPTime.tm_min+":"+NTPTime.tm_sec); */

  if (getEpData(ReqDevAddr)) // if we get valid data from the device?
  {
    getJsonData(ReqDevAddr); // put it in the json document
    notifyClients();         // and notify the client for new data
  }
  else
  {
    if (errorcode != 0 && millis() > (notifyTimer + 1000))
    {
      notifyClients(); // anyway, call the client something
      notifyTimer = millis();
    }
    else if (errorcode == 0)
    {
      notifyClients(); // anyway, call the client something
    }
  }

  // mqtt part, when time is come, fire up the mqtt function to send all data to the broker
  if ((millis() > (mqtttimer + (_settings.data.mqttRefresh * 1000)) || mqtttimer == 0) && !Update.isRunning())
  {
    tempSens.requestTemperatures();
    sendtoMQTT(); // Update data to MQTT server if we should
    mqtttimer = millis();
  }
  // select the next device adress, until the set amount of devices is reached, then set it to the first
  if (ReqDevAddr >= (size_t)_settings.data.deviceQuantity)
    ReqDevAddr = 1;
  else
    ReqDevAddr++;
  slowDownTimer = millis();
  return true;
}

static uint32_t wordsToUint32(uint16_t lowWord, uint16_t highWord)
{
  return (uint32_t)lowWord | ((uint32_t)highWord << 16);
}

static bool readInputBlock(uint16_t address, uint8_t count, uint16_t *values)
{
  epnode.clearResponseBuffer();
  result = epnode.readInputRegisters(address, count);
  if (result != epnode.ku8MBSuccess)
    return false;

  for (uint8_t index = 0; index < count; index++)
    values[index] = epnode.getResponseBuffer(index);
  return true;
}

static bool readHoldingBlock(uint16_t address, uint8_t count, uint16_t *values)
{
  epnode.clearResponseBuffer();
  result = epnode.readHoldingRegisters(address, count);
  if (result != epnode.ku8MBSuccess)
    return false;

  for (uint8_t index = 0; index < count; index++)
    values[index] = epnode.getResponseBuffer(index);
  return true;
}

EpeverProfile detectEpeverProfile(uint8_t device, bool force)
{
  if (device == 0 || device > MAX_DEVICES)
    return EpeverProfile::Unknown;
  if (!force && deviceProfiles[device] != EpeverProfile::Unknown)
    return deviceProfiles[device];

  epnode.setSlaveId(device);
  uint16_t model;
  if (!readInputBlock(NC_G3_MODEL, 1, &model))
    return EpeverProfile::Unknown;

  if (model <= 11)
    deviceProfiles[device] = EpeverProfile::ItNcG3;
  else if (model <= 23)
    deviceProfiles[device] = EpeverProfile::EtNcG3;
  else
    deviceProfiles[device] = EpeverProfile::Legacy;

  if (model <= 23)
    deviceModelIds[device] = model;
  return deviceProfiles[device];
}

static bool getLegacyEpData(int invNum);

static bool getNcG3EpData(int invNum, EpeverProfile profile)
{
  memset(&ncG3, 0, sizeof(ncG3));
  memset(rtc.buf, 0, sizeof(rtc.buf));
  uTime.setDateTime(0, 0, 0, 0, 0, 0);
  loadState = false;

  uint16_t values[20];
  ncG3.modelId = deviceModelIds[invNum];

  // Rated data. Only the model and charge-current rating are required for
  // profile selection and safe current-limit writes; other ratings are
  // optional because firmware revisions expose slightly different subsets.
  if (readInputBlock(0x3002, 1, values))
    ncG3.pvMaxVoltage = values[0];
  if (readInputBlock(0x3004, 4, values))
  {
    ncG3.ratedChargePower = wordsToUint32(values[0], values[1]);
    ncG3.ratedBatteryVoltage = values[2];
    ncG3.ratedChargeCurrent = values[3];
    deviceRatedChargeCurrent[invNum] = values[3];
  }
  if (profile == EpeverProfile::ItNcG3 && readInputBlock(0x300B, 1, values))
    ncG3.ratedLoadCurrent = values[0];
  if (readInputBlock(0x300E, 2, values))
  {
    ncG3.dspFirmware = values[0];
    ncG3.pvCount = values[1];
  }
  if (readInputBlock(0x3011, 1, values))
    ncG3.armFirmware = values[0];
  if (ncG3.pvCount < 1 || ncG3.pvCount > 2)
    ncG3.pvCount = 1;

  // Core live data.
  if (!readInputBlock(0x3100, 4, values))
    goto read_failed;
  ncG3.pv1Voltage = values[0];
  ncG3.pv1Current = values[1];
  ncG3.pv1Power = wordsToUint32(values[2], values[3]);

  if (ncG3.pvCount > 1 && readInputBlock(0x3108, 4, values))
  {
    ncG3.pv2Voltage = values[0];
    ncG3.pv2Current = values[1];
    ncG3.pv2Power = wordsToUint32(values[2], values[3]);
  }

  if (profile == EpeverProfile::ItNcG3)
  {
    if (!readInputBlock(0x3110, 4, values))
      goto read_failed;
    ncG3.loadVoltage = values[0];
    ncG3.loadCurrent = values[1];
    ncG3.loadPower = wordsToUint32(values[2], values[3]);
  }

  if (!readInputBlock(0x3114, 1, values))
    goto read_failed;
  ncG3.batteryVoltage = values[0];
  if (!readInputBlock(0x3117, 4, values))
    goto read_failed;
  ncG3.batteryCurrent = (int16_t)values[0];
  ncG3.batteryTemperature = (int16_t)values[1];
  ncG3.batterySoc = values[2];
  ncG3.deviceTemperature = (int16_t)values[3];
  if (!readInputBlock(0x311D, 5, values))
    goto read_failed;
  ncG3.systemVoltage = values[0];
  ncG3.highestPvVoltage = values[1];
  ncG3.totalPvCurrent = values[2];
  ncG3.totalPvPower = wordsToUint32(values[3], values[4]);

  if (!readInputBlock(0x3200, 4, values))
    goto read_failed;
  for (uint8_t index = 0; index < 4; index++)
    ncG3.status[index] = values[index];
  if (readInputBlock(0x3205, 1, values))
    ncG3.status[5] = values[0];

  if (!readInputBlock(0x3301, 2, values))
    goto read_failed;
  ncG3.batteryMaxToday = values[0];
  ncG3.batteryMinToday = values[1];

  if (profile == EpeverProfile::ItNcG3)
  {
    if (!readInputBlock(0x3303, 16, values))
      goto read_failed;
    ncG3.consumedDay = wordsToUint32(values[0], values[1]);
    ncG3.consumedMonth = wordsToUint32(values[2], values[3]);
    ncG3.consumedYear = wordsToUint32(values[4], values[5]);
    ncG3.consumedTotal = wordsToUint32(values[6], values[7]);
    ncG3.generatedDay = wordsToUint32(values[8], values[9]);
    ncG3.generatedMonth = wordsToUint32(values[10], values[11]);
    ncG3.generatedYear = wordsToUint32(values[12], values[13]);
    ncG3.generatedTotal = wordsToUint32(values[14], values[15]);
  }
  else
  {
    if (!readInputBlock(0x330B, 8, values))
      goto read_failed;
    ncG3.generatedDay = wordsToUint32(values[0], values[1]);
    ncG3.generatedMonth = wordsToUint32(values[2], values[3]);
    ncG3.generatedYear = wordsToUint32(values[4], values[5]);
    ncG3.generatedTotal = wordsToUint32(values[6], values[7]);
  }

  // Main battery settings, split around gaps in the G3 map.
  if (!readHoldingBlock(0x9000, 3, values))
    goto read_failed;
  ncG3.batteryType = values[0];
  ncG3.batteryCapacity = values[1];
  ncG3.temperatureCompensation = values[2];
  if (!readHoldingBlock(0x9007, 13, values))
    goto read_failed;
  ncG3.highVoltageDisconnect = values[0];
  ncG3.chargingLimitVoltage = values[1];
  ncG3.overVoltageReconnect = values[2];
  ncG3.equalizationVoltage = values[3];
  ncG3.boostVoltage = values[4];
  ncG3.floatVoltage = values[5];
  ncG3.boostReconnectVoltage = values[6];
  ncG3.lowVoltageReconnect = values[7];
  ncG3.underVoltageRecover = values[8];
  ncG3.underVoltageWarning = values[9];
  ncG3.lowVoltageDisconnect = values[10];
  ncG3.dischargingLimitVoltage = values[11];
  ncG3.chargingCurrentLimit = values[12];

  // Optional settings and the G3 RTC. Not all firmware exposes every group.
  if (readHoldingBlock(0x9014, 9, values))
  {
    ncG3.equalizationTime = values[0];
    ncG3.boostTime = values[1];
    ncG3.lithiumProtection = values[2];
    ncG3.lowTemperatureChargeLimit = (int16_t)values[3];
    ncG3.lowTemperatureDischargeLimit = (int16_t)values[4];
    rtc.buf[0] = values[5];
    rtc.buf[1] = values[6];
    rtc.buf[2] = values[7];
    ncG3.maximumBatteryTemperature = (int16_t)values[8];
    uTime.setDateTime(2000 + rtc.r.y, rtc.r.M, rtc.r.d, rtc.r.h, rtc.r.m, rtc.r.s);
  }
  if (readHoldingBlock(0x901D, 3, values))
  {
    ncG3.minimumBatteryTemperature = (int16_t)values[0];
    ncG3.maximumDeviceTemperature = (int16_t)values[1];
    ncG3.deviceTemperatureRecover = (int16_t)values[2];
  }
  if (readHoldingBlock(0x9038, 16, values))
  {
    ncG3.chargingMode = values[0];
    ncG3.fullSoc = values[1];
    ncG3.fullSocRecover = values[2];
    ncG3.dischargeRecoverSoc = values[3];
    ncG3.lowPowerRecoverSoc = values[4];
    ncG3.lowPowerAlarmSoc = values[5];
    ncG3.dischargeSoc = values[6];
    ncG3.recordPeriod = values[7];
    ncG3.bmsProtocol = values[8];
    ncG3.bmsEnabled = values[9];
    ncG3.pvInputMode = values[10];
    ncG3.modbusAddress = values[13];
    ncG3.baudRateCode = values[14];
    ncG3.parallelChargeCurrentLimit = values[15];
  }

  // The IT profile exposes detailed BMS telemetry. Keep this optional so a
  // controller without an active BMS still publishes its normal live data.
  if (profile == EpeverProfile::ItNcG3 && readInputBlock(0x3400, 10, values))
  {
    ncG3.bmsCellCount = values[0];
    ncG3.bmsPackVoltage = values[1];
    ncG3.bmsCurrent = (int16_t)values[2];
    ncG3.bmsFullCapacity = values[5];
    ncG3.bmsRemainingCapacity = values[6];
    ncG3.bmsRemainingMinutes = values[7];
    ncG3.bmsMaximumCellTemperature = (int16_t)values[8];
    ncG3.bmsMinimumCellTemperature = (int16_t)values[9];
    ncG3.bmsDataValid = true;
  }

  if (profile == EpeverProfile::ItNcG3)
  {
    epnode.clearResponseBuffer();
    result = epnode.readCoils(NC_G3_LOAD_STATE, 1);
    loadState = result == epnode.ku8MBSuccess
                    ? epnode.getResponseBuffer(0) != 0
                    : (ncG3.status[1] & 0x0001) != 0;
  }

  errorcode = 0;
  DEBUG_WEBLN("[" + String(invNum) + "] NC G3 transmission OK.");
  return true;

read_failed:
  errorcode = result;
  DEBUG_WEBLN("[" + String(invNum) + "] " + String(result) + " NC G3 register read failed");
  return false;
}

bool getEpData(int invNum)
{
  errorcode = 0;
  epnode.setSlaveId(invNum);
  EpeverProfile profile = detectEpeverProfile(invNum);

  if (profile == EpeverProfile::ItNcG3 || profile == EpeverProfile::EtNcG3)
    return getNcG3EpData(invNum, profile);

  return getLegacyEpData(invNum);
}

static bool getLegacyEpData(int invNum)
{
  errorcode = 0;
  epnode.setSlaveId(invNum);

  // clear buffers
  memset(rtc.buf, 0, sizeof(rtc.buf));
  memset(live.buf, 0, sizeof(live.buf));
  memset(stats.buf, 0, sizeof(stats.buf));
  batteryCurrent = 0;
  batterySOC = 0;
  uTime.setDateTime(0, 0, 0, 0, 0, 0);

  epnode.clearResponseBuffer();
  result = epnode.readHoldingRegisters(RTC_CLOCK, RTC_CLOCK_CNT);
  if (result == epnode.ku8MBSuccess)
  {
    rtc.buf[0] = epnode.getResponseBuffer(0);
    rtc.buf[1] = epnode.getResponseBuffer(1);
    rtc.buf[2] = epnode.getResponseBuffer(2);
    uTime.setDateTime((2000 + rtc.r.y), rtc.r.M, rtc.r.d, (rtc.r.h + 3), rtc.r.m, rtc.r.s);

    errorcode = result;
  }
  else
  {
    DEBUG_WEBLN("[" + String(invNum) + "] " + result + " Read registers for clock Failed");
    errorcode += result;
    return false;
  }
  // read LIVE-Data 0x3100
  epnode.clearResponseBuffer();
  result = epnode.readInputRegisters(LIVE_DATA, LIVE_DATA_CNT);
  if (result == epnode.ku8MBSuccess)
  {

    for (i = 0; i < LIVE_DATA_CNT; i++)
      live.buf[i] = epnode.getResponseBuffer(i);

    errorcode += result;
  }
  else
  {
    DEBUG_WEBLN("[" + String(invNum) + "] " + result + " Read LIVE-Dat Failed");
    errorcode += result;
    return false;
  }
  // Statistical Data 0x3300
  epnode.clearResponseBuffer();
  result = epnode.readInputRegisters(STATISTICS, STATISTICS_CNT);
  if (result == epnode.ku8MBSuccess)
  {
    for (i = 0; i < STATISTICS_CNT; i++)
      stats.buf[i] = epnode.getResponseBuffer(i);

    errorcode += result;
  }
  else
  {
    DEBUG_WEBLN("[" + String(invNum) + "] " + result + " Read Statistical Data Failed");
    errorcode += result;
    return false;
  }

  epnode.clearResponseBuffer();
  result = epnode.readInputRegisters(BATTERY_SOC, 1);
  if (result == epnode.ku8MBSuccess)
  {
    batterySOC = epnode.getResponseBuffer(0);
    errorcode += result;
  }
  else
  {
    DEBUG_WEBLN("[" + String(invNum) + "] " + result + " Read Battery SOC Failed");
    errorcode += result;
    return false;
  }
  // Battery Net Current = Icharge - Iload
  epnode.clearResponseBuffer();
  result = epnode.readInputRegisters(BATTERY_CURRENT_L, 2);
  if (result == epnode.ku8MBSuccess)
  {
    batteryCurrent = epnode.getResponseBuffer(0);
    batteryCurrent |= epnode.getResponseBuffer(1) << 16;
    errorcode += result;
  }
  else
  {
    DEBUG_WEBLN("[" + String(invNum) + "] " + result + " Read Battery Net Current = Icharge - Iload Failed");
    errorcode += result;
    return false;
  }
  // State of the Load Switch
  epnode.clearResponseBuffer();
  result = epnode.readCoils(LOAD_STATE, 1);
  if (result == epnode.ku8MBSuccess)
  {
    loadState = epnode.getResponseBuffer(0) ? true : false;
    errorcode += result;
  }
  else
  {
    DEBUG_WEBLN("[" + String(invNum) + "] " + result + " Read State of the Load Switch Failed");
    errorcode += result;
    return false;
  }
  // Read Status Flags
  epnode.clearResponseBuffer();
  result = epnode.readInputRegisters(0x3200, 2);
  if (result == epnode.ku8MBSuccess)
  {
    uint16_t temp = epnode.getResponseBuffer(0);
    status_batt.volt = temp & 0b1111;
    status_batt.temp = (temp >> 4) & 0b1111;
    status_batt.resistance = (temp >> 8) & 0b1;
    status_batt.rated_volt = (temp >> 15) & 0b1;

    temp = epnode.getResponseBuffer(1);

    // for(i=0; i<16; i++) DEBUG_WEB( (temp >> (15-i) ) & 1 );

    // charger_input     = ( temp & 0b0000000000000000 ) >> 15 ;
    charger_mode = (temp & 0b0000000000001100) >> 2;
    // charger_input     = ( temp & 0b0000000000000000 ) >> 12 ;
    // charger_operation = ( temp & 0b0000000000000000 ) >> 0 ;
    errorcode += result;
  }
  else
  {
    DEBUG_WEBLN("[" + String(invNum) + "] " + result + " Read Read Status Flags Failed");
    errorcode += result;
    return false;
  }

  epnode.clearResponseBuffer();
  result = epnode.readInputRegisters(DEVICE_TEMPERATURE, 1);
  if (result == epnode.ku8MBSuccess)
  {
    deviceTemperature = epnode.getResponseBuffer(0);
    errorcode += result;
  }
  else
  {
    DEBUG_WEBLN("[" + String(invNum) + "] " + result + " Read Device Temperature Failed");
    errorcode += result;
    return false;
  }

  epnode.clearResponseBuffer();
  result = epnode.readInputRegisters(BATTERY_TEMPERATURE, 1);
  if (result == epnode.ku8MBSuccess)
  {
    batteryTemperature = epnode.getResponseBuffer(0);
    errorcode += result;
  }
  else
  {
    DEBUG_WEBLN("[" + String(invNum) + "] " + result + " Read Battery temperature Failed");
    errorcode += result;
    return false;
  }

  epnode.clearResponseBuffer();
  result = epnode.readHoldingRegisters(DEVICE_SETTINGS, DEVICE_SETTINGS_CNT);
  if (result == epnode.ku8MBSuccess)
  {
    for (i = 0; i < DEVICE_SETTINGS_CNT; i++)
      settingParam.buf[i] = epnode.getResponseBuffer(i);
    errorcode += result;
  }
  else
  {
    DEBUG_WEBLN("[" + String(invNum) + "] " + result + " Read Settings Data Failed");
    errorcode += result;
    return false;
  }
  if (errorcode == 0)
  {
    DEBUG_WEBLN("[" + String(invNum) + "] Transmission OK.");
  }
  return true;
}

bool writeEpeverLoadState(uint8_t device, bool state)
{
  EpeverProfile profile = detectEpeverProfile(device);
  if (profile == EpeverProfile::Unknown || profile == EpeverProfile::EtNcG3)
    return false;

  epnode.setSlaveId(device);
  uint16_t coil = profile == EpeverProfile::ItNcG3 ? NC_G3_LOAD_STATE : LOAD_STATE;
  uint8_t writeStatus = epnode.writeSingleCoil(coil, state ? 1 : 0);
  delay(50);
  epnode.clearResponseBuffer();
  uint8_t readStatus = epnode.readCoils(coil, 1);
  return writeStatus == epnode.ku8MBSuccess &&
         readStatus == epnode.ku8MBSuccess &&
         (epnode.getResponseBuffer(0) != 0) == state;
}

bool writeNcG3ChargeCurrentLimit(uint8_t device, float amps)
{
  if (!isfinite(amps) || amps <= 0)
    return false;

  EpeverProfile profile = detectEpeverProfile(device);
  if (profile != EpeverProfile::ItNcG3 && profile != EpeverProfile::EtNcG3)
    return false;

  epnode.setSlaveId(device);
  uint16_t ratedRaw = deviceRatedChargeCurrent[device];
  if (ratedRaw == 0 && !readInputBlock(NC_G3_RATED_CHARGE_CURRENT, 1, &ratedRaw))
    return false;
  deviceRatedChargeCurrent[device] = ratedRaw;

  float scaled = amps * 100.0f;
  uint32_t requestedRaw = (uint32_t)roundf(scaled);
  if (requestedRaw == 0 || requestedRaw > ratedRaw || requestedRaw > UINT16_MAX ||
      fabsf(scaled - requestedRaw) > 0.01f)
    return false;

  uint8_t writeStatus = epnode.writeSingleRegister(NC_G3_CHARGE_CURRENT_LIMIT, (uint16_t)requestedRaw);
  delay(100);
  uint16_t readBack;
  bool verified = readHoldingBlock(NC_G3_CHARGE_CURRENT_LIMIT, 1, &readBack) &&
                  readBack == requestedRaw;
  if (verified)
    ncG3.chargingCurrentLimit = readBack;
  return writeStatus == epnode.ku8MBSuccess && verified;
}

static const char *ncG3BatteryVoltageState(uint8_t value)
{
  switch (value)
  {
  case 0:
    return "Normal";
  case 1:
    return "Overvoltage";
  case 2:
    return "Undervoltage";
  case 3:
    return "Low voltage disconnect";
  default:
    return "Fault";
  }
}

static const char *ncG3BatteryTemperatureState(uint8_t value)
{
  if (value == 0)
    return "Normal";
  if (value == 1)
    return "Over temperature";
  if (value == 2)
    return "Low temperature";
  return "Fault";
}

static bool getNcG3JsonData(int invNum, EpeverProfile profile)
{
  String deviceKey = "EP_" + String(invNum);
  JsonObject device = liveJson[deviceKey].to<JsonObject>();
  device.clear();
  JsonObject liveData = device["LiveData"].to<JsonObject>();
  JsonObject statsData = device["StatsData"].to<JsonObject>();
  JsonObject deviceData = device["DeviceData"].to<JsonObject>();

  liveData["CONNECTION"] = errorcode;
  liveData["DEVICE_NUM"] = String(invNum);
  liveData["DEVICE_TIME"] = uTime.getUnix();
  liveData["DEVICE_TEMP"] = ncG3.deviceTemperature / 100.f;
  liveData["SOLAR_V"] = ncG3.pv1Voltage / 100.f;
  liveData["SOLAR_A"] = ncG3.pv1Current / 100.f;
  liveData["SOLAR_W"] = ncG3.pv1Power / 100.f;
  if (ncG3.pvCount > 1)
  {
    liveData["SOLAR_2_V"] = ncG3.pv2Voltage / 100.f;
    liveData["SOLAR_2_A"] = ncG3.pv2Current / 100.f;
    liveData["SOLAR_2_W"] = ncG3.pv2Power / 100.f;
  }
  liveData["SOLAR_TOTAL_A"] = ncG3.totalPvCurrent / 100.f;
  liveData["SOLAR_TOTAL_W"] = ncG3.totalPvPower / 100.f;
  liveData["PV_HIGHEST_V"] = ncG3.highestPvVoltage / 100.f;

  liveData["BATT_SOC"] = ncG3.batterySoc;
  liveData["BATT_V"] = ncG3.batteryVoltage / 100.f;
  liveData["BATT_A"] = ncG3.batteryCurrent / 100.f;
  liveData["BATT_W"] = (ncG3.batteryVoltage / 100.f) * (ncG3.batteryCurrent / 100.f);
  liveData["BATT_STATE"] = ncG3BatteryVoltageState(ncG3.status[0] & 0x0f);
  liveData["BATT_TEMP"] = ncG3.batteryTemperature / 100.f;
  liveData["BATT_TEMP_STATE"] = ncG3BatteryTemperatureState((ncG3.status[0] >> 4) & 0x0f);
  liveData["SYSTEM_V"] = ncG3.systemVoltage / 100.f;
  liveData["LITHIUM_VOLTAGE_ID_ERROR"] = (ncG3.status[0] & 0x8000) != 0;

  bool loadAvailable = profile == EpeverProfile::ItNcG3;
  liveData["LOAD_AVAILABLE"] = loadAvailable;
  if (loadAvailable)
  {
    liveData["LOAD_V"] = ncG3.loadVoltage / 100.f;
    liveData["LOAD_A"] = ncG3.loadCurrent / 100.f;
    liveData["LOAD_W"] = ncG3.loadPower / 100.f;
    liveData["LOAD_STATE"] = loadState;
    liveData["LOAD_SHORT_CIRCUIT"] = (ncG3.status[1] & 0x0800) != 0;
    liveData["LOAD_OVERLOAD"] = (ncG3.status[1] >> 12) & 0x03;
  }

  charger_mode = (ncG3.status[2] >> 2) & 0x03;
  charger_input = (ncG3.status[2] >> 14) & 0x03;
  liveData["CHARGER_STATE"] = charger_input == 0 ? "Normal" : "Input overvoltage";
  liveData["CHARGER_MODE"] = charger_charging_status[charger_mode];
  liveData["DAYTIME"] = (ncG3.status[2] & 0x0002) != 0;
  liveData["DEVICE_OVERHEAT"] = (ncG3.status[2] & 0x0020) != 0;
  liveData["CHARGING_OVERHEAT"] = (ncG3.status[2] & 0x0080) != 0;
  liveData["REMOTE_CHARGING_ENABLED"] = (ncG3.status[3] & 0x0100) != 0;
  liveData["LOW_POWER"] = (ncG3.status[3] & 0x0200) != 0;
  liveData["MPPT_ACTIVE"] = (ncG3.status[3] & 0x0020) != 0;
  liveData["PV_MODE_ALARM"] = (ncG3.status[3] >> 10) & 0x03;
  liveData["PV2_INPUT_STATUS"] = (ncG3.status[3] >> 14) & 0x03;

  statsData["BATT_MAX"] = ncG3.batteryMaxToday / 100.f;
  statsData["BATT_MIN"] = ncG3.batteryMinToday / 100.f;
  statsData["CONSUMPTION_AVAILABLE"] = loadAvailable;
  if (loadAvailable)
  {
    statsData["CONS_DAY"] = ncG3.consumedDay / 100.f;
    statsData["CONS_MON"] = ncG3.consumedMonth / 100.f;
    statsData["CONS_YEAR"] = ncG3.consumedYear / 100.f;
    statsData["CONS_TOT"] = ncG3.consumedTotal / 100.f;
  }
  statsData["GEN_DAY"] = ncG3.generatedDay / 100.f;
  statsData["GEN_MON"] = ncG3.generatedMonth / 100.f;
  statsData["GEN_YEAR"] = ncG3.generatedYear / 100.f;
  statsData["GEN_TOT"] = ncG3.generatedTotal / 100.f;

  deviceData["DEVICE_PROFILE"] = profile == EpeverProfile::ItNcG3 ? "IT-NC G3" : "ET-NC G3";
  deviceData["DEVICE_MODEL"] = ncG3.modelId < 24 ? nc_g3_models[ncG3.modelId] : "Unknown NC G3";
  deviceData["MODEL_ID"] = ncG3.modelId;
  deviceData["PV_INPUT_COUNT"] = ncG3.pvCount;
  deviceData["PV_MAX_V"] = ncG3.pvMaxVoltage / 100.f;
  deviceData["RATED_CHARGE_POWER"] = ncG3.ratedChargePower / 100.f;
  deviceData["RATED_BATTERY_V"] = ncG3.ratedBatteryVoltage / 100.f;
  deviceData["RATED_CHARGE_A"] = ncG3.ratedChargeCurrent / 100.f;
  deviceData["RATED_LOAD_A"] = ncG3.ratedLoadCurrent / 100.f;
  deviceData["DSP_FIRMWARE"] = ncG3.dspFirmware / 100.f;
  deviceData["ARM_FIRMWARE"] = ncG3.armFirmware / 100.f;
  deviceData["BATTERY_TYPE"] = ncG3.batteryType < 13 ? nc_g3_battery_types[ncG3.batteryType] : "Unknown";
  deviceData["BATTERY_CAPACITY"] = ncG3.batteryCapacity;
  deviceData["TEMPERATURE_COMPENSATION"] = ncG3.temperatureCompensation / -100.f;
  deviceData["HIGH_VOLT_DISCONNECT"] = ncG3.highVoltageDisconnect / 100.f;
  deviceData["CHARGING_LIMIT_VOLTS"] = ncG3.chargingLimitVoltage / 100.f;
  deviceData["OVER_VOLTS_RECONNECT"] = ncG3.overVoltageReconnect / 100.f;
  deviceData["EQUALIZATION_VOLTS"] = ncG3.equalizationVoltage / 100.f;
  deviceData["BOOST_VOLTS"] = ncG3.boostVoltage / 100.f;
  deviceData["FLOAT_VOLTS"] = ncG3.floatVoltage / 100.f;
  deviceData["BOOST_RECONNECT_VOLTS"] = ncG3.boostReconnectVoltage / 100.f;
  deviceData["LOW_VOLTS_RECONNECT"] = ncG3.lowVoltageReconnect / 100.f;
  deviceData["UNDER_VOLTS_RECOVER"] = ncG3.underVoltageRecover / 100.f;
  deviceData["UNDER_VOLTS_WARNING"] = ncG3.underVoltageWarning / 100.f;
  deviceData["LOW_VOLTS_DISCONNECT"] = ncG3.lowVoltageDisconnect / 100.f;
  deviceData["DISCHARGING_LIMIT_VOLTS"] = ncG3.dischargingLimitVoltage / 100.f;
  deviceData["CHARGING_CURRENT_LIMIT"] = ncG3.chargingCurrentLimit / 100.f;
  deviceData["EQUALIZATION_TIME"] = ncG3.equalizationTime;
  deviceData["BOOST_TIME"] = ncG3.boostTime;
  deviceData["LITHIUM_PROTECTION"] = ncG3.lithiumProtection == 3;
  deviceData["LOW_TEMP_CHARGE_LIMIT"] = ncG3.lowTemperatureChargeLimit / 100.f;
  deviceData["LOW_TEMP_DISCHARGE_LIMIT"] = ncG3.lowTemperatureDischargeLimit / 100.f;
  deviceData["MAX_BATTERY_TEMP"] = ncG3.maximumBatteryTemperature / 100.f;
  deviceData["MIN_BATTERY_TEMP"] = ncG3.minimumBatteryTemperature / 100.f;
  deviceData["MAX_DEVICE_TEMP"] = ncG3.maximumDeviceTemperature / 100.f;
  deviceData["DEVICE_TEMP_RECOVER"] = ncG3.deviceTemperatureRecover / 100.f;
  deviceData["CHARGING_MODE_SETTING"] = ncG3.chargingMode == 0 ? "Voltage" : "SOC";
  deviceData["FULL_SOC"] = ncG3.fullSoc;
  deviceData["FULL_SOC_RECOVER"] = ncG3.fullSocRecover;
  deviceData["DISCHARGE_RECOVER_SOC"] = ncG3.dischargeRecoverSoc;
  deviceData["LOW_POWER_RECOVER_SOC"] = ncG3.lowPowerRecoverSoc;
  deviceData["LOW_POWER_ALARM_SOC"] = ncG3.lowPowerAlarmSoc;
  deviceData["DISCHARGE_SOC"] = ncG3.dischargeSoc;
  deviceData["RECORD_PERIOD"] = ncG3.recordPeriod;
  deviceData["BMS_PROTOCOL"] = ncG3.bmsProtocol;
  deviceData["BMS_ENABLED"] = ncG3.bmsEnabled != 0;
  deviceData["PV_INPUT_MODE"] = ncG3.pvInputMode == 0 ? "Independent" : "Centralized";
  deviceData["MODBUS_ADDRESS"] = ncG3.modbusAddress;
  deviceData["BAUD_RATE_CODE"] = ncG3.baudRateCode;
  deviceData["PARALLEL_CHARGE_CURRENT_LIMIT"] = ncG3.parallelChargeCurrentLimit;

  JsonObject bmsData = device["BmsData"].to<JsonObject>();
  bmsData["ONLINE"] = (ncG3.status[5] & 0x0001) != 0;
  bmsData["LOW_SOC"] = (ncG3.status[5] & 0x0002) != 0;
  bmsData["DISCHARGE_PROTECTION"] = (ncG3.status[5] & 0x0014) != 0;
  bmsData["CHARGE_PROTECTION"] = (ncG3.status[5] & 0x0408) != 0;
  bmsData["SENSOR_FAULT"] = (ncG3.status[5] & 0x0020) != 0;
  bmsData["CELL_LOW_TEMP"] = (ncG3.status[5] & 0x0040) != 0;
  bmsData["CELL_OVER_TEMP"] = (ncG3.status[5] & 0x0080) != 0;
  bmsData["CELL_LOW_VOLTAGE"] = (ncG3.status[5] & 0x0100) != 0;
  bmsData["CELL_OVER_VOLTAGE"] = (ncG3.status[5] & 0x0200) != 0;
  bmsData["FULL_SOC"] = (ncG3.status[5] & 0x2000) != 0;
  bmsData["DSP_COMMUNICATION_FAULT"] = (ncG3.status[5] & 0x4000) != 0;
  if (ncG3.bmsDataValid)
  {
    bmsData["CELL_COUNT"] = ncG3.bmsCellCount;
    bmsData["PACK_V"] = ncG3.bmsPackVoltage / 100.f;
    bmsData["PACK_A"] = ncG3.bmsCurrent / 100.f;
    bmsData["FULL_CAPACITY"] = ncG3.bmsFullCapacity;
    bmsData["REMAINING_CAPACITY"] = ncG3.bmsRemainingCapacity;
    bmsData["REMAINING_MINUTES"] = ncG3.bmsRemainingMinutes;
    bmsData["MAX_CELL_TEMP"] = ncG3.bmsMaximumCellTemperature / 100.f;
    bmsData["MIN_CELL_TEMP"] = ncG3.bmsMinimumCellTemperature / 100.f;
  }
  return true;
}

bool getJsonData(int invNum)
{
  EpeverProfile profile = invNum <= MAX_DEVICES ? deviceProfiles[invNum] : EpeverProfile::Unknown;
  if (profile == EpeverProfile::ItNcG3 || profile == EpeverProfile::EtNcG3)
  {
    getNcG3JsonData(invNum, profile);
    goto common_json;
  }

  //  for (size_t invNum = 1; invNum <= 3; invNum++) // for testing only{
  liveJson["EP_" + String(invNum)].clear();
  liveJson["EP_" + String(invNum)]["LiveData"]["CONNECTION"] = errorcode;

  liveJson["EP_" + String(invNum)]["LiveData"]["DEVICE_NUM"] = String(invNum); // for testing
  // device
  liveJson["EP_" + String(invNum)]["LiveData"]["DEVICE_TIME"] = uTime.getUnix();
  liveJson["EP_" + String(invNum)]["LiveData"]["DEVICE_TEMP"] = deviceTemperature / 100.f;
  // solar input
  liveJson["EP_" + String(invNum)]["LiveData"]["SOLAR_V"] = live.l.pvV / 100.f;
  liveJson["EP_" + String(invNum)]["LiveData"]["SOLAR_A"] = live.l.pvA / 100.f;
  liveJson["EP_" + String(invNum)]["LiveData"]["SOLAR_W"] = live.l.pvW / 100.f;
  // battery
  liveJson["EP_" + String(invNum)]["LiveData"]["BATT_SOC"] = batterySOC / 1.0f;
  liveJson["EP_" + String(invNum)]["LiveData"]["BATT_V"] = live.l.battV / 100.f;
  liveJson["EP_" + String(invNum)]["LiveData"]["BATT_A"] = batteryCurrent / 100.f;
  liveJson["EP_" + String(invNum)]["LiveData"]["BATT_W"] = (int(live.l.battV / 10) * int(batteryCurrent / 10) / 100.f);
  liveJson["EP_" + String(invNum)]["LiveData"]["BATT_STATE"] = batt_volt_status[status_batt.volt];
  liveJson["EP_" + String(invNum)]["LiveData"]["BATT_TEMP"] = batteryTemperature / 100.f;
  liveJson["EP_" + String(invNum)]["LiveData"]["BATT_TEMP_STATE"] = batt_temp_status[status_batt.temp];
  // load out
  liveJson["EP_" + String(invNum)]["LiveData"]["LOAD_V"] = live.l.loadV / 100.f;
  liveJson["EP_" + String(invNum)]["LiveData"]["LOAD_A"] = live.l.loadA / 100.f;
  liveJson["EP_" + String(invNum)]["LiveData"]["LOAD_W"] = live.l.loadW / 100.f;
  liveJson["EP_" + String(invNum)]["LiveData"]["LOAD_STATE"] = loadState;
  // charger
  liveJson["EP_" + String(invNum)]["LiveData"]["CHARGER_STATE"] = charger_input_status[charger_input];
  liveJson["EP_" + String(invNum)]["LiveData"]["CHARGER_MODE"] = charger_charging_status[charger_mode];
  // statistic
  liveJson["EP_" + String(invNum)]["StatsData"]["SOLAR_MAX"] = stats.s.pVmax / 100.f;
  liveJson["EP_" + String(invNum)]["StatsData"]["SOLAR_MIN"] = stats.s.pVmin / 100.f;
  liveJson["EP_" + String(invNum)]["StatsData"]["BATT_MAX"] = stats.s.bVmax / 100.f;
  liveJson["EP_" + String(invNum)]["StatsData"]["BATT_MIN"] = stats.s.bVmin / 100.f;
  liveJson["EP_" + String(invNum)]["StatsData"]["CONS_DAY"] = stats.s.consEnerDay / 100.f;
  liveJson["EP_" + String(invNum)]["StatsData"]["CONS_MON"] = stats.s.consEnerMon / 100.f;
  liveJson["EP_" + String(invNum)]["StatsData"]["CONS_YEAR"] = stats.s.consEnerYear / 100.f;
  liveJson["EP_" + String(invNum)]["StatsData"]["CONS_TOT"] = stats.s.consEnerTotal / 100.f;
  liveJson["EP_" + String(invNum)]["StatsData"]["GEN_DAY"] = stats.s.genEnerDay / 100.f;
  liveJson["EP_" + String(invNum)]["StatsData"]["GEN_MON"] = stats.s.genEnerMon / 100.f;
  liveJson["EP_" + String(invNum)]["StatsData"]["GEN_YEAR"] = stats.s.genEnerYear / 100.f;
  liveJson["EP_" + String(invNum)]["StatsData"]["GEN_TOT"] = stats.s.genEnerTotal / 100.f;
//  liveJson["EP_" + String(invNum)]["StatsData"]["CO2_REDUCTION"] = stats.s.c02Reduction / 100.f;
  // device settings data
  liveJson["EP_" + String(invNum)]["DeviceData"]["BATTERY_TYPE"] =
      settingParam.s.bTyp < (sizeof batt_type / sizeof batt_type[0]) ? batt_type[settingParam.s.bTyp] : "Unknown";
  liveJson["EP_" + String(invNum)]["DeviceData"]["BATTERY_CAPACITY"] = settingParam.s.bCapacity /*/ 100.f*/;
  liveJson["EP_" + String(invNum)]["DeviceData"]["TEMPERATURE_COMPENSATION"] = settingParam.s.tempCompensation / 100.f;
  liveJson["EP_" + String(invNum)]["DeviceData"]["HIGH_VOLT_DISCONNECT"] = settingParam.s.highVDisconnect / 100.f;
  liveJson["EP_" + String(invNum)]["DeviceData"]["CHARGING_LIMIT_VOLTS"] = settingParam.s.chLimitVolt / 100.f;
  liveJson["EP_" + String(invNum)]["DeviceData"]["OVER_VOLTS_RECONNECT"] = settingParam.s.overVoltRecon / 100.f;
  liveJson["EP_" + String(invNum)]["DeviceData"]["EQUALIZATION_VOLTS"] = settingParam.s.equVolt / 100.f;
  liveJson["EP_" + String(invNum)]["DeviceData"]["BOOST_VOLTS"] = settingParam.s.boostVolt / 100.f;
  liveJson["EP_" + String(invNum)]["DeviceData"]["FLOAT_VOLTS"] = settingParam.s.floatVolt / 100.f;
  liveJson["EP_" + String(invNum)]["DeviceData"]["BOOST_RECONNECT_VOLTS"] = settingParam.s.boostVoltRecon / 100.f;
  liveJson["EP_" + String(invNum)]["DeviceData"]["LOW_VOLTS_RECONNECT"] = settingParam.s.lowVoltRecon / 100.f;
  liveJson["EP_" + String(invNum)]["DeviceData"]["UNDER_VOLTS_RECOVER"] = settingParam.s.underVoltRecov / 100.f;
  liveJson["EP_" + String(invNum)]["DeviceData"]["UNDER_VOLTS_WARNING"] = settingParam.s.underVoltWarning / 100.f;
  liveJson["EP_" + String(invNum)]["DeviceData"]["LOW_VOLTS_DISCONNECT"] = settingParam.s.lowVoltDiscon / 100.f;
  liveJson["EP_" + String(invNum)]["DeviceData"]["DISCHARGING_LIMIT_VOLTS"] = settingParam.s.dischLimitVolt / 100.f;
  // }
common_json:
  liveJson["DEVICE_QUANTITY"] = _settings.data.deviceQuantity;
  liveJson["DEVICE_FREE_HEAP"] = ESP.getFreeHeap();
  // liveJson["DEVICE_FREE_JSON"] = (JSON_BUFFER - liveJson.memoryUsage());
  liveJson["ESP_VCC"] = (ESP.getVcc() / 1000.0) + 0.3;
  liveJson["Runtime"] = millis() / 1000;
  liveJson["Wifi_RSSI"] = WiFi.RSSI();
  liveJson["sw_version"] = SOFTWARE_VERSION;

  for (int i = 0; i < numOfTempSens; i++)
  {
    if (tempSens.getAddress(tempDeviceAddress, i))
    {
      liveJson["DS18B20_" + String(i + 1)] = tempSens.getTempC(tempDeviceAddress);
    }
  }
  return true;
}

bool connectMQTT()
{
  if (!mqttclient.connected())
  {
    if (mqttclient.connect(mqttClientId, _settings.data.mqttUser, _settings.data.mqttPassword, (topic + "/Alive").c_str(), 0, true, "false", true))
    {
      mqttclient.publish((topic + String("/IP")).c_str(), String(WiFi.localIP().toString()).c_str(), true);
      mqttclient.publish((topic + String("/Alive")).c_str(), "true", true); // LWT online message must be retained!

      if (strlen(_settings.data.mqttTriggerPath) > 0)
      {
        DEBUG_WEBLN("MQTT Data Trigger Subscribed");
        mqttclient.subscribe(_settings.data.mqttTriggerPath);
      }

      if (!_settings.data.mqttJson) // classic mqtt DP

        for (size_t i = 1; i < ((size_t)_settings.data.deviceQuantity + 1); i++)
        {
          mqttclient.subscribe((topic + "/" + devicePrefix + i + "/DeviceControl/LOAD_STATE").c_str());
          mqttclient.subscribe((topic + "/" + devicePrefix + i + "/DeviceControl/CHARGING_CURRENT_LIMIT").c_str());
        }
      else // subscribe json
        mqttclient.subscribe((topic + "/DATA").c_str());

      return true;
    }
    else
    {
      return false;
    }
    return false;
  }
  else
  {
    return true;
  }
}

bool sendtoMQTT()
{
  if (!connectMQTT())
  {
    return false;
  }
  mqttclient.publish((topic + String("/Alive")).c_str(), "true", true);
  mqttclient.publish((topic + String("/Wifi_RSSI")).c_str(), String(WiFi.RSSI()).c_str());
  if (!_settings.data.mqttJson)
  {
    for (JsonPair jsonDev : liveJson.as<JsonObject>())
    {
      if (String(jsonDev.key().c_str()).substring(0, 3) == "EP_")
      {
        for (JsonPair jsondat : jsonDev.value().as<JsonObject>())
        {
          for (JsonPair jsonVal : jsondat.value().as<JsonObject>())
          {
            char msgBuffer1[200];
            sprintf(msgBuffer1, "%s/%s/%s/%s", _settings.data.mqttTopic, jsonDev.key().c_str(), jsondat.key().c_str(), jsonVal.key().c_str());

            mqttclient.publish(msgBuffer1, jsonVal.value().as<String>().c_str());
          }
        }
      }
    }
    for (int i = 0; i < numOfTempSens; i++)
    {
      if (tempSens.getAddress(tempDeviceAddress, i))
      {
        char msgBuffer1[200];
        char valBufffer[8];
        sprintf(msgBuffer1, "%s/DS18B20_%i", _settings.data.mqttTopic, (i + 1));
        mqttclient.publish(msgBuffer1, dtostrf(tempSens.getTempC(tempDeviceAddress), 4, 2, valBufffer));
      }
    }
  }
  else
  {
    /*     mqttclient.beginPublish((topic + String("/DATA")).c_str(), measureJson(liveJson), false);
        serializeJson(liveJson, mqttclient);
        mqttclient.endPublish(); */

    mqttclient.beginPublish((topic + String("/DATA")).c_str(), measureJson(liveJson), false);
    BufferingPrint bufferedClient(mqttclient, 32);
    serializeJson(liveJson, bufferedClient);
    bufferedClient.flush();
    mqttclient.endPublish();
  }
  return true;
}

void callback(char *top, byte *payload, unsigned int length)
{
  // updateProgress = true; // stop servicing data

  JsonDocument mqttJsonAnswer;

  if (_settings.data.mqttJson)
  {
    DeserializationError err = deserializeJson(mqttJsonAnswer, (const byte *)payload, length);
    if (err)
    {
      Serial.print(F("MQTT JSON Parse Error: "));
      Serial.println(err.c_str());
      return;
    }

    for (size_t k = 1; k <= _settings.data.deviceQuantity; k++)
    {
      JsonVariant ep = mqttJsonAnswer[devicePrefix + k];
      if (!ep.isNull())
      {
        JsonVariant control = ep["DeviceControl"];
        if (!control.isNull() && !control["LOAD_STATE"].isNull())
        {
          mqtttimer = 0;
          workerCanRun = false;
          writeEpeverLoadState(k, control["LOAD_STATE"].as<bool>());
          workerCanRun = true;
        }
        if (!control.isNull() && !control["CHARGING_CURRENT_LIMIT"].isNull())
        {
          mqtttimer = 0;
          workerCanRun = false;
          writeNcG3ChargeCurrentLimit(k, control["CHARGING_CURRENT_LIMIT"].as<float>());
          workerCanRun = true;
        }
      }
    }
  }
  else
  {
    String messageTemp;
    for (unsigned int i = 0; i < length; i++)
    {
      messageTemp += (char)payload[i];
    }

    for (size_t k = 1; k <= _settings.data.deviceQuantity; k++)
    {
      if (strcmp(top, (topic + "/" + devicePrefix + k + "/DeviceControl/LOAD_STATE").c_str()) == 0)
      {
        mqtttimer = 0;
        workerCanRun = false;
        if (messageTemp == "true")
          writeEpeverLoadState(k, true);
        else if (messageTemp == "false")
          writeEpeverLoadState(k, false);
        workerCanRun = true;
      }
      if (strcmp(top, (topic + "/" + devicePrefix + k + "/DeviceControl/CHARGING_CURRENT_LIMIT").c_str()) == 0)
      {
        char *end = nullptr;
        float amps = strtof(messageTemp.c_str(), &end);
        if (end != messageTemp.c_str() && *end == '\0')
        {
          mqtttimer = 0;
          workerCanRun = false;
          writeNcG3ChargeCurrentLimit(k, amps);
          workerCanRun = true;
        }
      }
    }
  }

  if (strlen(_settings.data.mqttTriggerPath) > 0 && strcmp(top, _settings.data.mqttTriggerPath) == 0)
  {
    DEBUG_WEBLN("MQTT Data Trigger Firered Up");
    mqtttimer = 0;
  }

  // updateProgress = false; // start data servicing again
}


bool sendHaDiscovery()
{

  if (!connectMQTT())
  {
    return false;
  }

  char topBuff[128];

  for (JsonPair jsonDev : liveJson.as<JsonObject>())
  {
    if (String(jsonDev.key().c_str()).substring(0, 3) == "EP_")
    {
      String haDeviceDescription = String("\"dev\":") +
                                   "{\"ids\":[\"" + mqttClientId + "_" + jsonDev.key().c_str() + "\"]," +
                                   "\"name\":\"" + _settings.data.deviceName + "_" + jsonDev.key().c_str() + "\"," +
                                   "\"cu\":\"http://" + WiFi.localIP().toString() + "\"," +
                                   "\"mdl\":\"EPEver2MQTT" + "_" + jsonDev.key().c_str() + "\"," +
                                   "\"mf\":\"SoftWareCrash\"," +
                                   "\"sw\":\"" + SOFTWARE_VERSION + "\"" +
                                   "}";

      uint8_t deviceNumber = String(jsonDev.key().c_str()).substring(3).toInt();
      EpeverProfile profile = deviceNumber <= MAX_DEVICES ? deviceProfiles[deviceNumber] : EpeverProfile::Unknown;
      if (profile != EpeverProfile::EtNcG3)
      {
        String haSwitchPayLoad = String("{") +
                                 "\"name\":\"LOAD_STATE\"," +
                                 "\"command_topic\":\"" + _settings.data.mqttTopic + "/" + jsonDev.key().c_str() + "/DeviceControl/LOAD_STATE\"," +
                                 "\"stat_t\":\"" + _settings.data.mqttTopic + "/" + jsonDev.key().c_str() + "/LiveData/LOAD_STATE\"," +
                                 "\"avty_t\":\"" + _settings.data.mqttTopic + "/Alive\"," +
                                 "\"pl_avail\": \"true\"," +
                                 "\"pl_not_avail\": \"false\"," +
                                 "\"uniq_id\":\"" + mqttClientId + ".LOAD_STATE_" + jsonDev.key().c_str() + "\"," +
                                 "\"ic\":\"mdi:toggle-switch-off\"," +
                                 "\"pl_on\":\"true\"," +
                                 "\"pl_off\":\"false\"," +
                                 "\"stat_on\":\"true\"," +
                                 "\"stat_off\":\"false\",";

        haSwitchPayLoad += haDeviceDescription;
        haSwitchPayLoad += "}";
        sprintf(topBuff, "homeassistant/switch/%s_%s/LOAD_STATE/config", _settings.data.mqttTopic, jsonDev.key().c_str());
        mqttclient.beginPublish(topBuff, haSwitchPayLoad.length(), true);
        for (size_t i = 0; i < haSwitchPayLoad.length(); i++)
          mqttclient.write(haSwitchPayLoad[i]);
        mqttclient.endPublish();
      }

      if ((profile == EpeverProfile::ItNcG3 || profile == EpeverProfile::EtNcG3) &&
          deviceRatedChargeCurrent[deviceNumber] > 0)
      {
        float maximum = deviceRatedChargeCurrent[deviceNumber] / 100.f;
        String haNumberPayLoad = String("{") +
                                    "\"name\":\"CHARGING_CURRENT_LIMIT\"," +
                                    "\"command_topic\":\"" + _settings.data.mqttTopic + "/" + jsonDev.key().c_str() + "/DeviceControl/CHARGING_CURRENT_LIMIT\"," +
                                    "\"stat_t\":\"" + _settings.data.mqttTopic + "/" + jsonDev.key().c_str() + "/DeviceData/CHARGING_CURRENT_LIMIT\"," +
                                    "\"avty_t\":\"" + _settings.data.mqttTopic + "/Alive\"," +
                                    "\"pl_avail\":\"true\"," +
                                    "\"pl_not_avail\":\"false\"," +
                                    "\"uniq_id\":\"" + mqttClientId + ".CHARGING_CURRENT_LIMIT_" + jsonDev.key().c_str() + "\"," +
                                    "\"ic\":\"mdi:current-dc\"," +
                                    "\"unit_of_meas\":\"A\"," +
                                    "\"mode\":\"box\"," +
                                    "\"min\":0.01," +
                                    "\"max\":" + String(maximum, 2) + "," +
                                    "\"step\":0.01,";
        haNumberPayLoad += haDeviceDescription;
        haNumberPayLoad += "}";
        sprintf(topBuff, "homeassistant/number/%s_%s/CHARGING_CURRENT_LIMIT/config", _settings.data.mqttTopic, jsonDev.key().c_str());
        mqttclient.beginPublish(topBuff, haNumberPayLoad.length(), true);
        for (size_t i = 0; i < haNumberPayLoad.length(); i++)
          mqttclient.write(haNumberPayLoad[i]);
        mqttclient.endPublish();
      }
      // wifi
      String haPayLoad = String("{") +
                         "\"name\":\"Wifi_RSSI\"," +
                         "\"stat_t\":\"" + _settings.data.mqttTopic + "/Wifi_RSSI\"," +
                         "\"avty_t\":\"" + _settings.data.mqttTopic + "/Alive\"," +
                         "\"pl_avail\": \"true\"," +
                         "\"pl_not_avail\": \"false\"," +
                         "\"uniq_id\":\"" + mqttClientId + ".Wifi_RSSI_" + jsonDev.key().c_str() + "\"," +
                         "\"ic\":\"mdi:wifi-arrow-up-down\"," +
                         "\"unit_of_meas\":\"dB\"," +
                         "\"dev_cla\":\"signal_strength\",";
      haPayLoad += haDeviceDescription;
      haPayLoad += "}";
      sprintf(topBuff, "homeassistant/sensor/%s_%s/%s/config", _settings.data.mqttTopic, jsonDev.key().c_str(), "Wifi_RSSI"); // build the topic
      mqttclient.beginPublish(topBuff, haPayLoad.length(), true);
      for (size_t i = 0; i < haPayLoad.length(); i++)
      {
        mqttclient.write(haPayLoad[i]);
      }
      mqttclient.endPublish();
      // IP
      haPayLoad = String("{") +
                  "\"name\":\"IP\"," +
                  "\"stat_t\":\"" + _settings.data.mqttTopic + "/IP\"," +
                  "\"avty_t\":\"" + _settings.data.mqttTopic + "/Alive\"," +
                  "\"pl_avail\": \"true\"," +
                  "\"pl_not_avail\": \"false\"," +
                  "\"uniq_id\":\"" + mqttClientId + ".IP_" + jsonDev.key().c_str() + "\"," +
                  "\"ic\":\"mdi:ip-network\",";
      haPayLoad += haDeviceDescription;
      haPayLoad += "}";
      sprintf(topBuff, "homeassistant/sensor/%s_%s/%s/config", _settings.data.mqttTopic, jsonDev.key().c_str(), "IP"); // build the topic
      mqttclient.beginPublish(topBuff, haPayLoad.length(), true);
      for (size_t i = 0; i < haPayLoad.length(); i++)
      {
        mqttclient.write(haPayLoad[i]);
      }
      mqttclient.endPublish();

      for (JsonPair jsondat : jsonDev.value().as<JsonObject>())
      {
        for (JsonPair jsonVal : jsondat.value().as<JsonObject>())
        {
          for (size_t i = 0; i < sizeof haDescriptor / sizeof haDescriptor[0]; i++)
          {
            if (strcmp(jsonVal.key().c_str(), haDescriptor[i][0]) == 0)
            {
              String haPayLoad = String("{") +
                                 "\"name\":\"" + haDescriptor[i][0] + "\"," +
                                 "\"stat_t\":\"" + _settings.data.mqttTopic + "/" + jsonDev.key().c_str() + "/" + jsondat.key().c_str() + "/" + haDescriptor[i][0] + "\"," +
                                 "\"avty_t\":\"" + _settings.data.mqttTopic + "/Alive\"," +
                                 "\"pl_avail\": \"true\"," +
                                 "\"pl_not_avail\": \"false\"," +
                                 "\"uniq_id\":\"" + mqttClientId + "." + haDescriptor[i][0] + "_" + jsonDev.key().c_str() + "\"," +
                                 "\"ic\":\"mdi:" + haDescriptor[i][1] + "\",";
              if (strlen(haDescriptor[i][2]) != 0)
                haPayLoad += (String) "\"unit_of_meas\":\"" + haDescriptor[i][2] + "\",";

              if (strcmp(haDescriptor[i][2], "kWh") == 0 || strcmp(haDescriptor[i][2], "Wh") == 0)
                haPayLoad += (String) "\"state_class\":\"total_increasing\",";
              if (strcmp(haDescriptor[i][2], "A") == 0 || strcmp(haDescriptor[i][2], "V") == 0 || strcmp(haDescriptor[i][2], "W") == 0)
                haPayLoad += (String) "\"state_class\":\"measurement\",";

              if (strlen(haDescriptor[i][3]) != 0)
                haPayLoad += (String) "\"dev_cla\":\"" + haDescriptor[i][3] + "\",";

              haPayLoad += haDeviceDescription;
              haPayLoad += "}";
              sprintf(topBuff, "homeassistant/sensor/%s_%s/%s/config", _settings.data.mqttTopic, jsonDev.key().c_str(), haDescriptor[i][0]); // build the topic
              mqttclient.beginPublish(topBuff, haPayLoad.length(), true);
              for (size_t i = 0; i < haPayLoad.length(); i++)
              {
                mqttclient.write(haPayLoad[i]);
              }
              mqttclient.endPublish();
            }
          }
        }
      }
    }
  }
  // Ext Temp sensors
  for (int i = 0; i < numOfTempSens; i++)
  {
    if (tempSens.getAddress(tempDeviceAddress, i))
    {
      String haDeviceDescription = String("\"dev\":") +
                                   "{\"ids\":[\"" + mqttClientId + "\"]," +
                                   "\"name\":\"" + _settings.data.deviceName + "\"," +
                                   "\"cu\":\"http://" + WiFi.localIP().toString() + "\"," +
                                   "\"mdl\":\"EPEver2MQTT\"," +
                                   "\"mf\":\"SoftWareCrash\"," +
                                   "\"sw\":\"" + SOFTWARE_VERSION + "\"" +
                                   "}";

      String haPayLoad = String("{") +
                         "\"name\":\"DS18B20_" + (i + 1) + "\"," +
                         "\"stat_t\":\"" + _settings.data.mqttTopic + "/DS18B20_" + (i + 1) + "\"," +
                         "\"avty_t\":\"" + _settings.data.mqttTopic + "/Alive\"," +
                         "\"pl_avail\": \"true\"," +
                         "\"pl_not_avail\": \"false\"," +
                         "\"uniq_id\":\"" + mqttClientId + ".DS18B20_" + (i + 1) + "\"," +
                         "\"ic\":\"mdi:thermometer-lines\"," +
                         "\"unit_of_meas\":\"°C\"," +
                         "\"dev_cla\":\"temperature\",";
      haPayLoad += haDeviceDescription;
      haPayLoad += "}";
      sprintf(topBuff, "homeassistant/sensor/%s/DS18B20_%d/config", _settings.data.mqttTopic, (i + 1)); // build the topic

      mqttclient.beginPublish(topBuff, haPayLoad.length(), true);
      for (size_t i = 0; i < haPayLoad.length(); i++)
      {
        mqttclient.write(haPayLoad[i]);
      }
      mqttclient.endPublish();
    }
  }
  return true;
}
