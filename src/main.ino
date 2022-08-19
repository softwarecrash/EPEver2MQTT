/*Lot of ideas comes from here:
 * https://github.com/glitterkitty/EpEverSolarMonitor
 */
#include <Arduino.h>

#include <ModbusMaster.h>
#include "epregister.h"

#include <EEPROM.h>
#include <PubSubClient.h>

#include <ArduinoJson.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncWiFiManager.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "Settings.h" //settings functions

#include "webpages/HTMLcase.h"     // The HTML Konstructor
#include "webpages/main.h"         // landing page with menu
#include "webpages/settings.h"     // settings page
#include "webpages/settingsedit.h" // mqtt settings page

#define EPEVER_SERIAL Serial // Set the serial port for communication with the EPEver
#define EPEVER_BAUD 115200   // baud rate for modbus
#define EPEVER_DE_RE 5       // connect DE and Re to pin D1

WiFiClient client;
Settings _settings;
PubSubClient mqttclient(client);
int mqttBufferSize = 2048;

String topic = "/"; // Default first part of topic. We will add device ID in setup

unsigned long mqtttimer = 0;
unsigned long getDataTimer = 0;
AsyncWebServer server(80);
DNSServer dns;
ModbusMaster epnode; // instantiate ModbusMaster object
// flag for saving data and other things
bool shouldSaveConfig = false;
char mqtt_server[40];
bool restartNow = false;
bool updateProgress = false;
char timeBuff[256];  // buffer for timestamp
char msgBuffer[256]; // msgbuff for dtostrf
DynamicJsonDocument liveJson(mqttBufferSize);
JsonObject liveData = liveJson.createNestedObject("LiveData");
JsonObject statsData = liveJson.createNestedObject("StatsData");
//----------------------------------------------------------------------
void saveConfigCallback()
{
  shouldSaveConfig = true;
}

static void handle_update_progress_cb(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  uint32_t free_space = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
  if (!index)
  {
    Update.runAsync(true);
  }

  if (final)
  {
    if (!Update.end(true))
    {
    }
    else
    {
      AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Please wait while the device is booting new Firmware");
      response->addHeader("Refresh", "10; url=/");
      response->addHeader("Connection", "close");
      request->send(response);
      restartNow = true; // Set flag so main loop can issue restart call
    }
  }
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
  pinMode(EPEVER_DE_RE, OUTPUT);
  _settings.load();
  delay(500);
  WiFi.persistent(true);              // fix wifi save bug
  AsyncWiFiManager wm(&server, &dns); // create wifimanager instance
  EPEVER_SERIAL.begin(EPEVER_BAUD);
  epnode.begin(1, EPEVER_SERIAL);
  epnode.preTransmission(preTransmission);
  epnode.postTransmission(postTransmission);
  wm.setSaveConfigCallback(saveConfigCallback);

  AsyncWiFiManagerParameter custom_mqtt_server("mqtt_server", "MQTT server", NULL, 40);
  AsyncWiFiManagerParameter custom_mqtt_user("mqtt_user", "MQTT User", NULL, 40);
  AsyncWiFiManagerParameter custom_mqtt_pass("mqtt_pass", "MQTT Password", NULL, 100);
  AsyncWiFiManagerParameter custom_mqtt_topic("mqtt_topic", "MQTT Topic", NULL, 30);
  AsyncWiFiManagerParameter custom_mqtt_port("mqtt_port", "MQTT Port", NULL, 6);
  AsyncWiFiManagerParameter custom_mqtt_refresh("mqtt_refresh", "MQTT Send Interval", NULL, 4);
  AsyncWiFiManagerParameter custom_device_name("device_name", "Device Name", NULL, 40);

  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);
  wm.addParameter(&custom_mqtt_topic);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_refresh);
  wm.addParameter(&custom_device_name);

  bool res = wm.autoConnect("EPEver2MQTT-AP");

  wm.setConnectTimeout(30);       // how long to try to connect for before continuing
  wm.setConfigPortalTimeout(120); // auto close configportal after n seconds

  // save settings if wifi setup is fire up
  if (shouldSaveConfig)
  {
    _settings._mqttServer = custom_mqtt_server.getValue();
    _settings._mqttUser = custom_mqtt_user.getValue();
    _settings._mqttPassword = custom_mqtt_pass.getValue();
    _settings._mqttPort = atoi(custom_mqtt_port.getValue());
    _settings._deviceName = custom_device_name.getValue();
    _settings._mqttTopic = custom_mqtt_topic.getValue();
    _settings._mqttRefresh = atoi(custom_mqtt_refresh.getValue());

    _settings.save();
    delay(500);
    _settings.load();
    ESP.restart();
  }

  topic = _settings._mqttTopic;
  mqttclient.setServer(_settings._mqttServer.c_str(), _settings._mqttPort);
  mqttclient.setCallback(callback);
  mqttclient.setBufferSize(mqttBufferSize);
  // check is WiFi connected

  if (res)
  {
    // set the device name
    MDNS.begin(_settings._deviceName);
    WiFi.hostname(_settings._deviceName);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                AsyncResponseStream *response = request->beginResponseStream("text/html");
                response->printf_P(HTML_HEAD);
                response->printf_P(HTML_MAIN);
                response->printf_P(HTML_FOOT);
                request->send(response); });
    server.on("/livejson", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                AsyncResponseStream *response = request->beginResponseStream("application/json");
                liveJson["DEVICE_NAME"] = _settings._deviceName;
                serializeJson(liveJson, *response);
                request->send(response); });

    server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Please wait while the device reboots...");
                response->addHeader("Refresh", "15; url=/");
                response->addHeader("Connection", "close");
                request->send(response);
                restartNow = true; });
    server.on("/confirmreset", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                AsyncResponseStream *response = request->beginResponseStream("text/html");
                response->printf_P(HTML_HEAD);
                response->printf_P(HTML_CONFIRM_RESET);
                response->printf_P(HTML_FOOT);
                request->send(response); });

    server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Device is Erasing...");
                response->addHeader("Refresh", "15; url=/");
                response->addHeader("Connection", "close");
                request->send(response);
                delay(1000);
                _settings.reset();
                ESP.eraseConfig();
                ESP.restart(); });

    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                AsyncResponseStream *response = request->beginResponseStream("text/html");
                response->printf_P(HTML_HEAD);
                response->printf_P(HTML_SETTINGS);
                response->printf_P(HTML_FOOT);
                request->send(response); });

    server.on("/settingsedit", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                AsyncResponseStream *response = request->beginResponseStream("text/html");
                response->printf_P(HTML_HEAD);
                response->printf_P(HTML_SETTINGS_EDIT);
                response->printf_P(HTML_FOOT);
                request->send(response); });

    server.on("/settingsjson", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                AsyncResponseStream *response = request->beginResponseStream("application/json");
                DynamicJsonDocument SettingsJson(256);
                SettingsJson["device_name"] = _settings._deviceName;
                SettingsJson["mqtt_server"] = _settings._mqttServer;
                SettingsJson["mqtt_port"] = _settings._mqttPort;
                SettingsJson["mqtt_topic"] = _settings._mqttTopic;
                SettingsJson["mqtt_user"] = _settings._mqttUser;
                SettingsJson["mqtt_password"] = _settings._mqttPassword;
                SettingsJson["mqtt_refresh"] = _settings._mqttRefresh;
                SettingsJson["mqtt_json"] = _settings._mqttJson?true:false;
                serializeJson(SettingsJson, *response);
                request->send(response); });

    server.on("/settingssave", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                request->redirect("/settings");
                _settings._mqttServer = request->arg("post_mqttServer");
                _settings._mqttPort = request->arg("post_mqttPort").toInt();
                _settings._mqttUser = request->arg("post_mqttUser");
                _settings._mqttPassword = request->arg("post_mqttPassword");
                _settings._mqttTopic = request->arg("post_mqttTopic");
                _settings._mqttRefresh = request->arg("post_mqttRefresh").toInt();
                _settings._deviceName = request->arg("post_deviceName");
                if(request->arg("post_mqttjson") == "true") _settings._mqttJson = true;
                if(request->arg("post_mqttjson") != "true") _settings._mqttJson = false;
                Serial.print(_settings._mqttServer);
                _settings.save();
                delay(500);
                _settings.load(); });

    server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request)
              {
      AsyncWebParameter *p = request->getParam(0);
      if (p->name() == "loadstate")
      {
        if (p->value().toInt() == 1)
        {
          epnode.writeSingleCoil(0x0002, 1);
        }
        if (p->value().toInt() == 0)
        {
          epnode.writeSingleCoil(0x0002, 0);
        }
      }
      if (p->name() == "datetime")
      {
        uint8_t rtcSetY  = atoi (request->getParam("datetime")->value().substring(0, 2).c_str ()); //stunde
        uint8_t rtcSetM  = atoi (request->getParam("datetime")->value().substring(2, 4).c_str ()); //tag
        uint8_t rtcSetD  = atoi (request->getParam("datetime")->value().substring(4, 6).c_str ()); //tag
        uint8_t rtcSeth  = atoi (request->getParam("datetime")->value().substring(6, 8).c_str ()); //tag
        uint8_t rtcSetm  = atoi (request->getParam("datetime")->value().substring(8, 10).c_str ()); //tag
        uint8_t rtcSets  = atoi (request->getParam("datetime")->value().substring(10, 12).c_str ()); //tag
        epnode.setTransmitBuffer(0, ((uint16_t)rtcSetm << 8) | rtcSets); //demnach minute | sekunde
        epnode.setTransmitBuffer(1, ((uint16_t)rtcSetD << 8) | rtcSeth); //geht!!!!!!  tag | stunde
        epnode.setTransmitBuffer(2, ((uint16_t)rtcSetY << 8) | rtcSetM); //und Jahr | Monat
        epnode.writeMultipleRegisters(0x9013, 3); //write registers
        }
     request->send(200, "text/plain", "message received"); });

    server.on(
        "/update", HTTP_POST, [](AsyncWebServerRequest *request)
        {
          updateProgress = true;
          delay(500);
          request->send(200);
          request->redirect("/"); },
        handle_update_progress_cb);

    server.begin();
    MDNS.addService("http", "tcp", 80);
  }

  if (!mqttclient.connected())
    mqttclient.connect((String(_settings._deviceName)).c_str(), _settings._mqttUser.c_str(), _settings._mqttPassword.c_str());
  if (mqttclient.connect(_settings._deviceName.c_str()))
  {
    if (!_settings._mqttJson)
    {
      mqttclient.subscribe((String(topic) + String("/LOAD_STATE")).c_str());
    }
    else
    {
      mqttclient.subscribe((String(topic + "/" + _settings._deviceName)).c_str());
    }
  }
}
// end void setup

//----------------------------------------------------------------------
void loop()
{
  // Make sure wifi is in the right mode
  if (WiFi.status() == WL_CONNECTED)
  { // No use going to next step unless WIFI is up and running.
    MDNS.update();
    mqttclient.loop(); // Check if we have something to read from MQTT

    if (millis() > (getDataTimer + (2 * 1000)) && !updateProgress)
    {
      getEpData(); // get actual data from epever and set it to the json
      getDataTimer = millis();
    }
    if (!updateProgress)
      sendtoMQTT(); // Update data to MQTT server if we should
  }
  if (restartNow)
  {
    delay(1000);
    Serial.println("Restart");
    ESP.restart();
  }

  yield();
}
// End void loop

void getEpData()
{
  // clear buffers
  memset(rtc.buf, 0, sizeof(rtc.buf));
  memset(live.buf, 0, sizeof(live.buf));
  memset(stats.buf, 0, sizeof(stats.buf));

  // Read registers for clock
  epnode.clearResponseBuffer();
  result = epnode.readHoldingRegisters(RTC_CLOCK, RTC_CLOCK_CNT);
  if (result == epnode.ku8MBSuccess)
  {
    rtc.buf[0] = epnode.getResponseBuffer(0);
    rtc.buf[1] = epnode.getResponseBuffer(1);
    rtc.buf[2] = epnode.getResponseBuffer(2);
  }

  // read LIVE-Data
  epnode.clearResponseBuffer();
  result = epnode.readInputRegisters(LIVE_DATA, LIVE_DATA_CNT);
  if (result == epnode.ku8MBSuccess)
  {

    for (i = 0; i < LIVE_DATA_CNT; i++)
      live.buf[i] = epnode.getResponseBuffer(i);
  }

  // Statistical Data
  epnode.clearResponseBuffer();
  result = epnode.readInputRegisters(STATISTICS, STATISTICS_CNT);
  if (result == epnode.ku8MBSuccess)
  {
    for (i = 0; i < STATISTICS_CNT; i++)
      stats.buf[i] = epnode.getResponseBuffer(i);
  }

  // Battery SOC
  epnode.clearResponseBuffer();
  result = epnode.readInputRegisters(BATTERY_SOC, 1);
  if (result == epnode.ku8MBSuccess)
  {

    batterySOC = epnode.getResponseBuffer(0);
  }

  // Battery Net Current = Icharge - Iload
  epnode.clearResponseBuffer();
  result = epnode.readInputRegisters(BATTERY_CURRENT_L, 2);
  if (result == epnode.ku8MBSuccess)
  {
    batteryCurrent = epnode.getResponseBuffer(0);
    batteryCurrent |= epnode.getResponseBuffer(1) << 16;
  }

  // State of the Load Switch
  epnode.clearResponseBuffer();
  result = epnode.readCoils(LOAD_STATE, 1);
  if (result == epnode.ku8MBSuccess)
  {
    loadState = epnode.getResponseBuffer(0) ? true : false;
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

    // for(i=0; i<16; i++) Serial.print( (temp >> (15-i) ) & 1 );
    // Serial.println();

    // charger_input     = ( temp & 0b0000000000000000 ) >> 15 ;
    charger_mode = (temp & 0b0000000000001100) >> 2;
    // charger_input     = ( temp & 0b0000000000000000 ) >> 12 ;
    // charger_operation = ( temp & 0b0000000000000000 ) >> 0 ;

    // Serial.print( "charger_input : "); Serial.println( charger_input );
    // Serial.print( "charger_mode  : "); Serial.println( charger_mode );
    // Serial.print( "charger_oper  : "); Serial.println( charger_operation );
    // Serial.print( "charger_state : "); Serial.println( charger_state );
  }

  getJsonData(); // put the collected data into json fields
}

void getJsonData()
{
  snprintf(timeBuff, sizeof(timeBuff)-1, "20%02d-%02d-%02d %02d:%02d:%02d",
          rtc.r.y, rtc.r.M, rtc.r.d, rtc.r.h, rtc.r.m, rtc.r.s);
  liveJson["DEVICE_TIME"] = timeBuff;
  liveData["SOLAR_VOLTS"] = live.l.pV / 100.f;
  liveData["SOLAR_AMPS"] = live.l.pI / 100.f;
  liveData["SOLAR_WATTS"] = live.l.pP / 100.f;
  liveData["BATT_VOLTS"] = live.l.bV / 100.f;
  liveData["BATT_AMPS"] = live.l.bI / 100.f;
  liveData["BATT_WATTS"] = live.l.bP / 100.f;
  liveData["LOAD_VOLTS"] = live.l.lV / 100.f;
  liveData["LOAD_AMPS"] = live.l.lI / 100.f;
  liveData["LOAD_WATTS"] = live.l.lP / 100.f;
  liveData["BATTERY_SOC"] = batterySOC / 1.0f;
  liveJson["LOAD_STATE"] = loadState;

  statsData["SOLAR_MAX"] = stats.s.pVmax / 100.f;
  statsData["SOLAR_MIN"] = stats.s.pVmin / 100.f;
  statsData["BATT_MAX"] = stats.s.bVmax / 100.f;
  statsData["BATT_MIN"] = stats.s.bVmin / 100.f;

  statsData["CONS_ENERGY_DAY"] = stats.s.consEnerDay / 100.f;
  statsData["CONS_ENGERY_MON"] = stats.s.consEnerMon / 100.f;
  statsData["CONS_ENGERY_YEAR"] = stats.s.consEnerYear / 100.f;
  statsData["CONS_ENGERY_TOT"] = stats.s.consEnerTotal / 100.f;
  statsData["GEN_ENERGY_DAY"] = stats.s.genEnerDay / 100.f;
  statsData["GEN_ENERGY_MON"] = stats.s.genEnerMon / 100.f;
  statsData["GEN_ENERGY_YEAR"] = stats.s.genEnerYear / 100.f;
  statsData["GEN_ENERGY_TOT"] = stats.s.genEnerTotal / 100.f;
  statsData["CO2_REDUCTION"] = stats.s.c02Reduction / 100.f;

  liveJson["BATT_VOLT_STATUS"] = batt_volt_status[status_batt.volt];
  liveJson["BATT_TEMP"] = batt_temp_status[status_batt.temp];

  liveJson["CHARGER_INPUT_STATUS"] = charger_input_status[charger_input];
  liveJson["CHARGER_MODE"] = charger_charging_status[charger_mode];
}

bool sendtoMQTT()
{
  if (millis() < (mqtttimer + (_settings._mqttRefresh * 1000)) || _settings._mqttRefresh == 0)
  {
    return false;
  }
  mqtttimer = millis();
  if (!mqttclient.connected())
  {
    if (mqttclient.connect((String(_settings._deviceName)).c_str(), _settings._mqttUser.c_str(), _settings._mqttPassword.c_str()))
    {
      if (!_settings._mqttJson)
      {
        mqttclient.publish((topic + String("/IP")).c_str(), String(WiFi.localIP().toString()).c_str());
      }
    }
    else
    {
      return false; // Exit if we couldnt connect to MQTT brooker
    }
  }

  if (!_settings._mqttJson)
  {
    char msgBuffer[20];

    mqttclient.publish((String(topic) + String("/DEVICE_TIME")).c_str(), liveJson["DEVICE_TIME"]);
    mqttclient.publish((String(topic) + String("/LOAD_STATE")).c_str(), liveJson["LOAD_STATE"] ? "true" : "false");
    mqttclient.publish((String(topic) + String("/BATT_VOLT_STATUS")).c_str(), liveJson["BATT_VOLT_STATUS"]);
    mqttclient.publish((String(topic) + String("/BATT_TEMP")).c_str(), liveJson["BATT_TEMP"]);
    mqttclient.publish((String(topic) + String("/CHARGER_INPUT_STATUS")).c_str(), liveJson["CHARGER_INPUT_STATUS"]);
    mqttclient.publish((String(topic) + String("/CHARGER_MODE")).c_str(), liveJson["CHARGER_MODE"]);

    mqttclient.publish((String(topic) + String("/LiveData/SOLAR_VOLTS")).c_str(), liveData["SOLAR_VOLTS"]);
    mqttclient.publish((String(topic) + String("/LiveData/SOLAR_AMPS")).c_str(), liveData["SOLAR_AMPS"]);
    mqttclient.publish((String(topic) + String("/LiveData/SOLAR_WATTS")).c_str(), liveData["SOLAR_WATTS"]);

    mqttclient.publish((String(topic) + String("/LiveData/BATT_VOLTS")).c_str(), liveData["BATT_VOLTS"]);
    mqttclient.publish((String(topic) + String("/LiveData/BATT_AMPS")).c_str(), liveData["BATT_AMPS"]);
    mqttclient.publish((String(topic) + String("/LiveData/BATT_WATTS")).c_str(), liveData["BATT_WATTS"]);

    mqttclient.publish((String(topic) + String("/LiveData/LOAD_VOLTS")).c_str(), liveData["LOAD_VOLTS"]);
    mqttclient.publish((String(topic) + String("/LiveData/LOAD_AMPS")).c_str(), liveData["LOAD_AMPS"]);
    mqttclient.publish((String(topic) + String("/LiveData/LOAD_WATTS")).c_str(), liveData["LOAD_WATTS"]);

    mqttclient.publish((String(topic) + String("/LiveData/BATTERY_SOC")).c_str(), liveData["BATTERY_SOC"]);

    mqttclient.publish((String(topic) + String("/StatsData/SOLAR_MAX")).c_str(), statsData["SOLAR_MAX"]);
    mqttclient.publish((String(topic) + String("/StatsData/SOLAR_MIN")).c_str(), statsData["SOLAR_MIN"]);
    mqttclient.publish((String(topic) + String("/StatsData/BATT_MAX")).c_str(), statsData["BATT_MAX"]);
    mqttclient.publish((String(topic) + String("/StatsData/BATT_MIN")).c_str(), statsData["BATT_MIN"]);

    mqttclient.publish((String(topic) + String("/StatsData/CONS_ENERGY_DAY")).c_str(), statsData["CONS_ENERGY_DAY"]);
    mqttclient.publish((String(topic) + String("/StatsData/CONS_ENGERY_MON")).c_str(), statsData["CONS_ENGERY_MON"]);
    mqttclient.publish((String(topic) + String("/StatsData/CONS_ENGERY_YEAR")).c_str(), statsData["CONS_ENGERY_YEAR"]);
    mqttclient.publish((String(topic) + String("/StatsData/CONS_ENGERY_TOT")).c_str(), statsData["CONS_ENGERY_TOT"]);
    mqttclient.publish((String(topic) + String("/StatsData/GEN_ENERGY_DAY")).c_str(), statsData["GEN_ENERGY_DAY"]);
    mqttclient.publish((String(topic) + String("/StatsData/GEN_ENERGY_MON")).c_str(), statsData["GEN_ENERGY_MON"]);
    mqttclient.publish((String(topic) + String("/StatsData/GEN_ENERGY_YEAR")).c_str(), statsData["GEN_ENERGY_YEAR"]);
    mqttclient.publish((String(topic) + String("/StatsData/GEN_ENERGY_TOT")).c_str(), statsData["GEN_ENERGY_TOT"]);
    mqttclient.publish((String(topic) + String("/StatsData/CO2_REDUCTION")).c_str(), statsData["CO2_REDUCTION"]);
  }
  else
  {
    char mqttBuffer[2048];
    size_t n = serializeJson(liveJson, mqttBuffer);
    mqttclient.publish((String(topic + "/" + _settings._deviceName)).c_str(), mqttBuffer, n);
  }

  return true;
}

void callback(char *top, byte *payload, unsigned int length)
{
  if (!_settings._mqttJson)
  {
    String messageTemp;
    for (unsigned int i = 0; i < length; i++)
    {
      messageTemp += (char)payload[i];
    }
    // Switch the Discharging port
    if (strcmp(top, (topic + "/LOAD_STATE").c_str()) == 0)
    {
      if (messageTemp == "true")
      {
        epnode.writeSingleCoil(0x0002, 1);
      }
      if (messageTemp == "false")
      {
        epnode.writeSingleCoil(0x0002, 0);
      }
    }
  }
  else
  {
    StaticJsonDocument<1024> mqttJsonAnswer;
    deserializeJson(mqttJsonAnswer, (const byte *)payload, length);
    if (mqttJsonAnswer["LOAD_STATE"] == true)
    {
      epnode.writeSingleCoil(0x0002, 1);
    }
    else if (mqttJsonAnswer["LOAD_STATE"] == false)
    {
      epnode.writeSingleCoil(0x0002, 0);
    }
  }
}