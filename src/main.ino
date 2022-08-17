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

#include "Settings.h"

#include "webpages/HTMLcase.h"     // The HTML Konstructor
#include "webpages/main.h"         // landing page with menu
#include "webpages/settings.h"     // settings page
#include "webpages/settingsedit.h" // mqtt settings page

#define EPEVER_SERIAL Serial        // Set the serial port for communication with the EPEver
#define EPEVER_BAUD 115200          // baud rate for modbus
#define EPEVER_DE_RE 5             // connect DE and Re to pin D1
#define EPEVER_SERIAL_DEBUG Serial1 // Uncomment the below #define to enable debugging print statements.

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

//----------------------------------------------------------------------
void saveConfigCallback()
{
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

static void handle_update_progress_cb(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  uint32_t free_space = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
  if (!index)
  {
    EPEVER_SERIAL_DEBUG.println("Update");
    Update.runAsync(true);
    if (!Update.begin(free_space))
    {
      Update.printError(EPEVER_SERIAL_DEBUG);
    }
  }

  if (Update.write(data, len) != len)
  {
    Update.printError(EPEVER_SERIAL_DEBUG);
  }

  if (final)
  {
    if (!Update.end(true))
    {
      Update.printError(EPEVER_SERIAL_DEBUG);
    }
    else
    {

      AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Please wait while the device is booting new Firmware");
      response->addHeader("Refresh", "10; url=/");
      response->addHeader("Connection", "close");
      request->send(response);

      restartNow = true; // Set flag so main loop can issue restart call
      EPEVER_SERIAL_DEBUG.println("Update complete");
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


int loadState;

int batterySOC;

  uint8_t i, result;

float test;



void setup()
{
  pinMode(EPEVER_DE_RE, OUTPUT);
  digitalWrite(EPEVER_DE_RE, 0);
#ifdef EPEVER_SERIAL_DEBUG
  // This is needed to print stuff to the serial monitor
  EPEVER_SERIAL_DEBUG.begin(9600);
#endif

  _settings.load();
  delay(500);
  WiFi.persistent(true);              // fix wifi save bug
  AsyncWiFiManager wm(&server, &dns); // create wifimanager instance
  EPEVER_SERIAL.begin(EPEVER_BAUD);
  epnode.begin(1, EPEVER_SERIAL);
  epnode.preTransmission(preTransmission);
  epnode.postTransmission(postTransmission);









#ifdef EPEVER_SERIAL_DEBUG
  wm.setDebugOutput(false);
#endif
  wm.setSaveConfigCallback(saveConfigCallback);

#ifdef EPEVER_SERIAL_DEBUG
  EPEVER_SERIAL_DEBUG.begin(9600); // Debugging towards UART1
#endif

#ifdef EPEVER_SERIAL_DEBUG
  EPEVER_SERIAL_DEBUG.println();
  EPEVER_SERIAL_DEBUG.printf("Device Name:\t");
  EPEVER_SERIAL_DEBUG.println(_settings._deviceName);
  EPEVER_SERIAL_DEBUG.printf("Mqtt Server:\t");
  EPEVER_SERIAL_DEBUG.println(_settings._mqttServer);
  EPEVER_SERIAL_DEBUG.printf("Mqtt Port:\t");
  EPEVER_SERIAL_DEBUG.println(_settings._mqttPort);
  EPEVER_SERIAL_DEBUG.printf("Mqtt User:\t");
  EPEVER_SERIAL_DEBUG.println(_settings._mqttUser);
  EPEVER_SERIAL_DEBUG.printf("Mqtt Passwort:\t");
  EPEVER_SERIAL_DEBUG.println(_settings._mqttPassword);
  EPEVER_SERIAL_DEBUG.printf("Mqtt Interval:\t");
  EPEVER_SERIAL_DEBUG.println(_settings._mqttRefresh);
  EPEVER_SERIAL_DEBUG.printf("Mqtt Topic:\t");
  EPEVER_SERIAL_DEBUG.println(_settings._mqttTopic);
#endif
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
  // mqttclient.setCallback(callback);
  mqttclient.setBufferSize(mqttBufferSize);
  // check is WiFi connected
  if (!res)
  {
#ifdef EPEVER_SERIAL_DEBUG
    EPEVER_SERIAL_DEBUG.println("Failed to connect or hit timeout");
#endif
  }
  else
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
                DynamicJsonDocument liveJson(256);
                liveJson["device_name"] = _settings._deviceName;
                liveJson["packV"] = (String)test;
                liveJson["packA"] = (String)loadState;
                liveJson["packSOC"] = (String)batterySOC;
                //liveJson["packRes"] = (String)ep.get.resCapacitymAh;
               // liveJson["packCycles"] = (String)ep.get.bmsCycles;
                //liveJson["packTemp"] = (String)ep.get.cellTemperature[0];
                //liveJson["cellH"] = (String)ep.get.maxCellVNum + ". " + (String)(ep.get.maxCellmV / 1000);
                //liveJson["cellL"] = (String)ep.get.minCellVNum + ". " + (String)(ep.get.minCellmV / 1000);
                //liveJson["cellDiff"] = (String)ep.get.cellDiff;
                //liveJson["disChFet"] = ep.get.disChargeFetState? true : false;
                //liveJson["chFet"] = ep.get.chargeFetState? true : false;
                //liveJson["cellBal"] = ep.get.cellBalanceActive? true : false;
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
                if (p->name() == "chargefet")
                {
#ifdef EPEVER_SERIAL_DEBUG
                    EPEVER_SERIAL_DEBUG.println("charge fet webswitch to: "+(String)p->value());
#endif
                    if(p->value().toInt() == 1){
                      //ep.setChargeMOS(true);
                      //ep.get.chargeFetState = true;
                    }
                    if(p->value().toInt() == 0){
                      //ep.setChargeMOS(false);
                     // ep.get.chargeFetState = false;
                    }
                }
                if (p->name() == "dischargefet")
                {
#ifdef EPEVER_SERIAL_DEBUG
                    EPEVER_SERIAL_DEBUG.println("discharge fet webswitch to: "+(String)p->value());
#endif
                    if(p->value().toInt() == 1){
                      //ep.setDischargeMOS(true);
                      //ep.get.disChargeFetState = true;
                    }
                    if(p->value().toInt() == 0){
                      //ep.setDischargeMOS(false);
                      //ep.get.disChargeFetState = false;
                    }
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
#ifdef EPEVER_SERIAL_DEBUG
    EPEVER_SERIAL_DEBUG.println("Webserver Running...");
#endif
  }

  if (!mqttclient.connected())
    mqttclient.connect((String(_settings._deviceName)).c_str(), _settings._mqttUser.c_str(), _settings._mqttPassword.c_str());
  if (mqttclient.connect(_settings._deviceName.c_str()))
  {
    if (!_settings._mqttJson)
    {
      // mqttclient.subscribe((String(topic) + String("/Pack DischargeFET")).c_str());
      // mqttclient.subscribe((String(topic) + String("/Pack ChargeFET")).c_str());
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

    if (millis() > (getDataTimer + (3 * 1000)) && !updateProgress)
    {
      getEpData(); //get actual data from epever and set it to the json
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
  epnode.clearResponseBuffer();
  result = epnode.readInputRegisters(0x3100, 8);
  if (result == epnode.ku8MBSuccess)
  {
    test = epnode.getResponseBuffer(BATT_VOLTS)/100.0f;
  }  else  {
    //Serial.print("Miss read batterySOC, ret val:");
    test = 199;
  }

  
  if (epnode.readInputRegisters(BATTERY_SOC, 1) == epnode.ku8MBSuccess) batterySOC = epnode.getResponseBuffer(0);
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
#ifdef EPEVER_SERIAL_DEBUG
      EPEVER_SERIAL_DEBUG.println(F("Reconnected to MQTT SERVER"));
#endif
      if (!_settings._mqttJson)
      {
        mqttclient.publish((topic + String("/IP")).c_str(), String(WiFi.localIP().toString()).c_str());
      }
    }
    else
    {
#ifdef EPEVER_SERIAL_DEBUG
      EPEVER_SERIAL_DEBUG.println(F("CANT CONNECT TO MQTT"));
#endif
      return false; // Exit if we couldnt connect to MQTT brooker
    }
  }
#ifdef EPEVER_SERIAL_DEBUG
  EPEVER_SERIAL_DEBUG.println(F("Data sent to MQTT Server"));
#endif

















  if (!_settings._mqttJson)
  {
    char msgBuffer[20];
    /*
        mqttclient.publish((String(topic) + String("/Pack Voltage")).c_str(), dtostrf(ep.get.packVoltage, 4, 1, msgBuffer));
        mqttclient.publish((String(topic) + String("/Pack Current")).c_str(), dtostrf(ep.get.packCurrent, 4, 1, msgBuffer));
        mqttclient.publish((String(topic) + String("/Pack SOC")).c_str(), dtostrf(ep.get.packSOC, 6, 2, msgBuffer));
        mqttclient.publish((String(topic) + String("/Pack Remaining mAh")).c_str(), String(ep.get.resCapacitymAh).c_str());
        mqttclient.publish((String(topic) + String("/Pack Cycles")).c_str(), String(ep.get.bmsCycles).c_str());
        mqttclient.publish((String(topic) + String("/Pack Min Temperature")).c_str(), String(ep.get.tempMin).c_str());
        mqttclient.publish((String(topic) + String("/Pack Max Temperature")).c_str(), String(ep.get.tempMax).c_str());
        mqttclient.publish((String(topic) + String("/Pack High Cell")).c_str(), (dtostrf(ep.get.maxCellVNum, 1, 0, msgBuffer) + String(".- ") + dtostrf(ep.get.maxCellmV / 1000, 5, 3, msgBuffer)).c_str());
        mqttclient.publish((String(topic) + String("/Pack Low Cell")).c_str(), (dtostrf(ep.get.minCellVNum, 1, 0, msgBuffer) + String(".- ") + dtostrf(ep.get.minCellmV / 1000, 5, 3, msgBuffer)).c_str());
        mqttclient.publish((String(topic) + String("/Pack Cell Difference")).c_str(), String(ep.get.cellDiff).c_str());
        mqttclient.publish((String(topic) + String("/Pack ChargeFET")).c_str(), ep.get.chargeFetState ? "true" : "false");
        mqttclient.publish((String(topic) + String("/Pack DischargeFET")).c_str(), ep.get.disChargeFetState ? "true" : "false");
        mqttclient.publish((String(topic) + String("/Pack Status")).c_str(), String(ep.get.chargeDischargeStatus).c_str());
        mqttclient.publish((String(topic) + String("/Pack Cells")).c_str(), String(ep.get.numberOfCells).c_str());
        mqttclient.publish((String(topic) + String("/Pack Heartbeat")).c_str(), String(ep.get.bmsHeartBeat).c_str());
        mqttclient.publish((String(topic) + String("/Pack Balance Active")).c_str(), String(ep.get.cellBalanceActive ? "true" : "false").c_str());

        for (size_t i = 0; i < size_t(ep.get.numberOfCells); i++)
        {
          mqttclient.publish((String(topic) + String("/Pack Cells Voltage/Cell ") + (String)(i + 1)).c_str(), dtostrf(ep.get.cellVmV[i] / 1000, 5, 3, msgBuffer));
          mqttclient.publish((String(topic) + String("/Pack Cells Balance/Cell ") + (String)(i + 1)).c_str(), String(ep.get.cellBalanceState[i] ? "true" : "false").c_str());
        }
        for (size_t i = 0; i < size_t(ep.get.numOfTempSensors); i++)
        {
          mqttclient.publish((String(topic) + String("/Pack Temperature Sensor No ") + (String)(i + 1)).c_str(), String(ep.get.cellTemperature[i]).c_str());
        }
        */
  }
  else
  {
    char mqttBuffer[2048];
    DynamicJsonDocument mqttJson(mqttBufferSize);
    /*
        JsonObject mqttJsonPack = mqttJson.createNestedObject("Pack");
        mqttJsonPack["Device IP"] = WiFi.localIP().toString();
        mqttJsonPack["Voltage"] = ep.get.packVoltage;
        mqttJsonPack["Current"] = ep.get.packCurrent;
        mqttJsonPack["SOC"] = ep.get.packSOC;
        mqttJsonPack["Remaining mAh"] = ep.get.resCapacitymAh;
        mqttJsonPack["Cycles"] = ep.get.bmsCycles;
        //mqttJsonPack["MinTemp"] = ep.get.tempMin;
        //mqttJsonPack["MaxTemp"] = ep.get.tempMax;
        mqttJsonPack["High CellNr"] = ep.get.maxCellVNum;
        mqttJsonPack["High CellV"] = ep.get.maxCellmV / 1000;
        mqttJsonPack["Low CellNr"] = ep.get.minCellVNum;
        mqttJsonPack["Low CellV"] = ep.get.minCellmV / 1000;
        mqttJsonPack["Cell Difference"] = ep.get.cellDiff;
        mqttJsonPack["DischargeFET"] = ep.get.disChargeFetState;
        mqttJsonPack["ChargeFET"] = ep.get.chargeFetState;
        mqttJsonPack["Status"] = ep.get.chargeDischargeStatus;
        mqttJsonPack["Cells"] = ep.get.numberOfCells;
        mqttJsonPack["Heartbeat"] = ep.get.bmsHeartBeat;
        mqttJsonPack["Balance Active"] = ep.get.cellBalanceActive;

        JsonObject mqttJsonCellV = mqttJson.createNestedObject("CellV");
        for (size_t i = 0; i < size_t(ep.get.numberOfCells); i++)
        {
          //put this data into an array later!
          mqttJsonCellV["CellV " + String(i + 1)] = ep.get.cellVmV[i] / 1000;
          //mqttJsonCellV["Balance " + String(i + 1)] = ep.get.cellBalanceState[i];
        }
        JsonObject mqttJsonTemp = mqttJson.createNestedObject("CellTemp");
        for (size_t i = 0; i < size_t(ep.get.numOfTempSensors); i++)
        {
          mqttJsonTemp["Temp" + String(i + 1)] = ep.get.cellTemperature[i];
        }

        size_t n = serializeJson(mqttJson, mqttBuffer);
        mqttclient.publish((String(topic + "/" + _settings._deviceName)).c_str(), mqttBuffer, n);
        */
  }

  return true;
}
/*
void callback(char *top, byte *payload, unsigned int length)
{
  if (!_settings._mqttJson)
  {
    String messageTemp;
    for (unsigned int i = 0; i < length; i++)
    {
      messageTemp += (char)payload[i];
    }
#ifdef EPEVER_SERIAL_DEBUG
    EPEVER_SERIAL_DEBUG.println("message recived: " + messageTemp);
#endif
    // Switch the Discharging port
    if (strcmp(top, (topic + "/Pack DischargeFET").c_str()) == 0)
    {
#ifdef EPEVER_SERIAL_DEBUG
      EPEVER_SERIAL_DEBUG.println("message recived: " + messageTemp);
#endif

      if (messageTemp == "true")
      {
#ifdef EPEVER_SERIAL_DEBUG
        EPEVER_SERIAL_DEBUG.println("switching Discharging mos on");
#endif
        ep.setDischargeMOS(true);
      }
      if (messageTemp == "false")
      {
#ifdef EPEVER_SERIAL_DEBUG
        EPEVER_SERIAL_DEBUG.println("switching Discharging mos off");
#endif
        ep.setDischargeMOS(false);
      }
    }

    // Switch the Charging Port
    if (strcmp(top, (topic + "/Pack ChargeFET").c_str()) == 0)
    {
#ifdef EPEVER_SERIAL_DEBUG
      EPEVER_SERIAL_DEBUG.println("message recived: " + messageTemp);
#endif

      if (messageTemp == "true")
      {
#ifdef EPEVER_SERIAL_DEBUG
        EPEVER_SERIAL_DEBUG.println("switching Charging mos on");
#endif
        ep.setChargeMOS(true);
      }
      if (messageTemp == "false")
      {
#ifdef EPEVER_SERIAL_DEBUG
        EPEVER_SERIAL_DEBUG.println("switching Charging mos off");
#endif
        ep.setChargeMOS(false);
      }
    }
  }
  else
  {
    StaticJsonDocument<1024> mqttJsonAnswer;
    deserializeJson(mqttJsonAnswer, (const byte *)payload, length);

    if (mqttJsonAnswer["Pack"]["ChargeFET"] == true)
    {
      ep.setChargeMOS(true);
    }
    else if (mqttJsonAnswer["Pack"]["ChargeFET"] == false)
    {
      ep.setChargeMOS(false);
    }
    else
    {
#ifdef EPEVER_SERIAL_DEBUG
      EPEVER_SERIAL_DEBUG.println("No Valid Command from JSON for setChargeMOS");
#endif
    }
    if (mqttJsonAnswer["Pack"]["DischargeFET"] == true)
    {
      ep.setDischargeMOS(true);
    }
    else if (mqttJsonAnswer["Pack"]["DischargeFET"] == false)
    {
      ep.setDischargeMOS(false);
    }
    else
    {
#ifdef EPEVER_SERIAL_DEBUG
      EPEVER_SERIAL_DEBUG.println("No Valid Command from JSON for setDischargeMOS");
#endif
    }
  }
}
*/
