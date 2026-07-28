#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "../app/PollingControl.h"
#include "../epever/EpeverController.h"

class MqttService;

class WebSocketController
{
public:
  WebSocketController(AsyncWebSocket &socket, const JsonDocument &liveJson,
                      PollingControl &pollingControl,
                      EpeverController &controller, MqttService &mqtt);

  void begin();
  void notify();
  void cleanup();

private:
  void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len);
  void handleMessage(void *arg, uint8_t *data, size_t len);

  AsyncWebSocket &_socket;
  const JsonDocument &_liveJson;
  PollingControl &_pollingControl;
  EpeverController &_controller;
  MqttService &_mqtt;
};
