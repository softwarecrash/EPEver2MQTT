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
void NTPTimeSetCB() {
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
      epnode.setSlaveId(String((char *)data).substring(11, 12).toInt());
      epnode.writeSingleCoil(0x0002, String((char *)data).substring(13, 14).toInt());
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
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_PING:
  case WS_EVT_ERROR:
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

void setup()
{
  pinMode(EPEVER_DE_RE, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  analogWrite(LED_PIN, 0);
  resetCounter(true);
  _settings.load();
  WiFi.persistent(true);              // fix wifi save bug
  AsyncWiFiManager wm(&server, &dns); // create wifimanager instance

  EPEVER_SERIAL.begin(EPEVER_BAUD);
  epnode.setResponseTimeout(100);
  epnode.begin(1, EPEVER_SERIAL);
  epnode.preTransmission(preTransmission);
  epnode.postTransmission(postTransmission);

  wm.setSaveConfigCallback(saveConfigCallback);


  //https://werner.rothschopf.net/202011_arduino_esp8266_ntp_en.htm
  if(strlen(_settings.data.NTPTimezone) != 0 && strlen(_settings.data.NTPServer) != 0){
    configTime(_settings.data.NTPTimezone, _settings.data.NTPServer);
    settimeofday_cb(NTPTimeSetCB);
  }

  sprintf(mqttClientId, "%s-%06X", _settings.data.deviceName, ESP.getChipId());

  AsyncWiFiManagerParameter custom_mqtt_server("mqtt_server", "MQTT server", NULL, 128);
  AsyncWiFiManagerParameter custom_mqtt_user("mqtt_user", "MQTT User", NULL, 32);
  AsyncWiFiManagerParameter custom_mqtt_pass("mqtt_pass", "MQTT Password", NULL, 32);
  AsyncWiFiManagerParameter custom_mqtt_topic("mqtt_topic", "MQTT Topic", "EPEver", 32);
  AsyncWiFiManagerParameter custom_mqtt_port("mqtt_port", "MQTT Port", NULL, 6);
  AsyncWiFiManagerParameter custom_mqtt_refresh("mqtt_refresh", "MQTT Send Interval", "300", 4);
  AsyncWiFiManagerParameter custom_mqtt_triggerpath("mqtt_triggerpath", "MQTT Data Trigger Path", NULL, 80);
  AsyncWiFiManagerParameter custom_device_name("device_name", "Device Name", "EPEver2MQTT", 32);
  AsyncWiFiManagerParameter custom_device_quantity("device_quantity", "Device Quantity", "1", 2);

  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);
  wm.addParameter(&custom_mqtt_topic);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_refresh);
  wm.addParameter(&custom_mqtt_triggerpath);
  wm.addParameter(&custom_device_name);
  wm.addParameter(&custom_device_quantity);

  bool res = wm.autoConnect("EPEver2MQTT-AP");

  // wm.setConnectTimeout(30);       // how long to try to connect for before continuing
  wm.setConfigPortalTimeout(120); // auto close configportal after n seconds

  // save settings if wifi setup is fire up
  if (shouldSaveConfig)
  {
    strncpy(_settings.data.mqttServer, custom_mqtt_server.getValue(), 128);
    strncpy(_settings.data.mqttUser, custom_mqtt_user.getValue(), 40);
    strncpy(_settings.data.mqttPassword, custom_mqtt_pass.getValue(), 40);
    _settings.data.mqttPort = atoi(custom_mqtt_port.getValue());
    strncpy(_settings.data.deviceName, custom_device_name.getValue(), 40);
    strncpy(_settings.data.mqttTopic, custom_mqtt_topic.getValue(), 40);
    _settings.data.mqttRefresh = atoi(custom_mqtt_refresh.getValue());
    strncpy(_settings.data.mqttTriggerPath, custom_mqtt_triggerpath.getValue(), 80);
    _settings.data.deviceQuantity = atoi(custom_device_quantity.getValue()) <= 0 ? 1 : atoi(custom_device_quantity.getValue());

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

    server.on("/settingssave", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                if(strlen(_settings.data.httpUser) > 0 && !request->authenticate(_settings.data.httpUser, _settings.data.httpPass)) return request->requestAuthentication();
                strncpy(_settings.data.mqttServer, request->arg("post_mqttServer").c_str(), 128);
                _settings.data.mqttPort = request->arg("post_mqttPort").toInt();
                strncpy(_settings.data.mqttUser, request->arg("post_mqttUser").c_str(), 40);
                strncpy(_settings.data.mqttPassword, request->arg("post_mqttPassword").c_str(), 40);
                strncpy(_settings.data.mqttTopic, request->arg("post_mqttTopic").c_str(), 40);
                _settings.data.mqttRefresh = request->arg("post_mqttRefresh").toInt() < 1 ? 1 : request->arg("post_mqttRefresh").toInt(); // prevent lower numbers
                strncpy(_settings.data.deviceName, request->arg("post_deviceName").c_str(), 40);
                _settings.data.deviceQuantity = request->arg("post_deviceQuanttity").toInt() <= 0 ? 1 : request->arg("post_deviceQuanttity").toInt();
                _settings.data.mqttJson = (request->arg("post_mqttjson") == "true") ? true : false;
                strncpy(_settings.data.mqttTriggerPath, request->arg("post_mqtttrigger").c_str(), 80);
                _settings.data.webUIdarkmode = (request->arg("post_webuicolormode") == "true") ? true : false;
                strncpy(_settings.data.httpUser, request->arg("post_httpUser").c_str(), 40);
                strncpy(_settings.data.httpPass, request->arg("post_httpPass").c_str(), 40);
                _settings.data.haDiscovery = (request->arg("post_hadiscovery") == "true") ? true : false;
                strncpy(_settings.data.NTPTimezone, request->arg("post_ntptimezone").c_str(), 40);
                strncpy(_settings.data.NTPServer, request->arg("post_ntptimeserv").c_str(), 40);
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
          epnode.setTransmitBuffer(0, ((uint16_t)rtcSetm << 8) | rtcSets); // minute | secund
          epnode.setTransmitBuffer(1, ((uint16_t)rtcSetD << 8) | rtcSeth); // day | hour
          epnode.setTransmitBuffer(2, ((uint16_t)rtcSetY << 8) | rtcSetM); // year | month
          epnode.writeMultipleRegisters(0x9013, 3); //write registers
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
    //webSerial.onMessage(recvMsg);

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
  {                      // No use going to next step unless WIFI is up and running.
    ws.cleanupClients(); // clean unused client connections

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



  if(strlen(_settings.data.NTPTimezone) != 0 && setNTPTimeToDevice == true){
    time(&timeNow);
    localtime_r(&timeNow, &NTPTime); 
    for (size_t i = 1; i <= ((size_t)_settings.data.deviceQuantity); i++)
    {
      epnode.setSlaveId(i);
      epnode.setTransmitBuffer(0, ((uint16_t)NTPTime.tm_min << 8) | NTPTime.tm_sec); // minute | secund
      epnode.setTransmitBuffer(1, ((uint16_t)NTPTime.tm_mday << 8) | NTPTime.tm_hour); // day | hour
      epnode.setTransmitBuffer(2, ((uint16_t)(NTPTime.tm_year - 100) << 8) | (NTPTime.tm_mon + 1)); // year | month
      epnode.writeMultipleRegisters(0x9013, 3); //write registers
      delay(50);
    }
   DEBUG_WEBLN((String) NTPTime.tm_mday+"."+(NTPTime.tm_mon + 1)+"."+(NTPTime.tm_year + 1900 )+" "+NTPTime.tm_hour+":"+NTPTime.tm_min+":"+NTPTime.tm_sec);
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

bool getEpData(int invNum)
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

bool getJsonData(int invNum)
{

  //  for (size_t invNum = 1; invNum <= 3; invNum++) // for testing only{
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
  liveJson["EP_" + String(invNum)]["StatsData"]["CO2_REDUCTION"] = stats.s.c02Reduction / 100.f;
  // device settings data
  liveJson["EP_" + String(invNum)]["DeviceData"]["BATTERY_TYPE"] = batt_type[settingParam.s.bTyp];
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
  liveJson["DEVICE_QUANTITY"] = _settings.data.deviceQuantity;
  liveJson["DEVICE_FREE_HEAP"] = ESP.getFreeHeap();
  //liveJson["DEVICE_FREE_JSON"] = (JSON_BUFFER - liveJson.memoryUsage());
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
      mqttclient.publish((topic + String("/IP")).c_str(), String(WiFi.localIP().toString()).c_str());
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
  if (!_settings.data.mqttJson)
  {
    String messageTemp;
    for (unsigned int i = 0; i < length; i++)
    {
      messageTemp += (char)payload[i];
    }

    for (size_t k = 1; k <= ((size_t)_settings.data.deviceQuantity); k++)
    {
      if (strcmp(top, (topic + "/" + devicePrefix + k + "/DeviceControl/LOAD_STATE").c_str()) == 0)
      {
        epnode.setSlaveId(k);
        mqtttimer = 0;
        if (messageTemp == "true")
          epnode.writeSingleCoil(0x0002, 1);
        if (messageTemp == "false")
          epnode.writeSingleCoil(0x0002, 0);
      }
    }
  }
  else
  {
    JsonDocument mqttJsonAnswer;
    deserializeJson(mqttJsonAnswer, (const byte *)payload, length);

    for (size_t k = 1; k < ((size_t)_settings.data.deviceQuantity + 1); k++)
    {
    // if (mqttJsonAnswer.containsKey(devicePrefix + k))
      if (mqttJsonAnswer[devicePrefix + k].is<const char*>())
      {
        epnode.setSlaveId(k);
        mqtttimer = 0;
        if (mqttJsonAnswer[devicePrefix + k]["LiveData"]["LOAD_STATE"] == true)
          epnode.writeSingleCoil(0x0002, 1);
        if (mqttJsonAnswer[devicePrefix + k]["LiveData"]["LOAD_STATE"] == false)
          epnode.writeSingleCoil(0x0002, 0);
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
      sprintf(topBuff, "homeassistant/switch/%s_%s/LOAD_STATE/config", _settings.data.mqttTopic, jsonDev.key().c_str()); // build the topic

      mqttclient.beginPublish(topBuff, haSwitchPayLoad.length(), true);
      for (size_t i = 0; i < haSwitchPayLoad.length(); i++)
      {
        mqttclient.write(haSwitchPayLoad[i]);
      }
      mqttclient.endPublish();
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