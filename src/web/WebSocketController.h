#pragma once

#ifndef ARDUINOJSON_USE_DOUBLE
#define ARDUINOJSON_USE_DOUBLE 0
#endif
#ifndef ARDUINOJSON_USE_LONG_LONG
#define ARDUINOJSON_USE_LONG_LONG 1
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

class WebSocketController
{
public:
  using WriteLoadStateFn = bool (*)(uint8_t device, bool state);

  WebSocketController(AsyncWebSocket &socket, JsonDocument &liveJson,
                      bool &workerCanRun, unsigned long &mqttTimer,
                      WriteLoadStateFn writeLoadState);

  void begin();
  void notify();
  void cleanup();

private:
  void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len);
  void handleMessage(void *arg, uint8_t *data, size_t len);

  AsyncWebSocket &_socket;
  JsonDocument &_liveJson;
  bool &_workerCanRun;
  unsigned long &_mqttTimer;
  WriteLoadStateFn _writeLoadState;
};
