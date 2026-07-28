#include "WebSocketController.h"

#include "../app/DiagnosticLog.h"
#include "../mqtt/MqttService.h"

WebSocketController::WebSocketController(
    AsyncWebSocket &socket, const JsonDocument &liveJson,
    PollingControl &pollingControl, EpeverController &controller,
    MqttService &mqtt)
    : _socket(socket),
      _liveJson(liveJson),
      _pollingControl(pollingControl),
      _controller(controller),
      _mqtt(mqtt)
{
}

void WebSocketController::begin()
{
  _socket.onEvent(
      [this](AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void *arg, uint8_t *data, size_t len)
      { onEvent(server, client, type, arg, data, len); });
}

void WebSocketController::notify()
{
  const size_t length = measureJson(_liveJson);
  AsyncWebSocketMessageBuffer *buffer = _socket.makeBuffer(length);
  if (buffer == nullptr)
    return;

  // The websocket buffer contains exactly the payload bytes. Passing a
  // capacity of length + 1 lets ArduinoJson append a null terminator beyond
  // the allocated buffer and corrupts the heap.
  serializeJson(_liveJson, reinterpret_cast<char *>(buffer->get()), length);
  _socket.textAll(buffer);
}

void WebSocketController::cleanup()
{
  _socket.cleanupClients();
}

void WebSocketController::onEvent(AsyncWebSocket *, AsyncWebSocketClient *client,
                                  AwsEventType type, void *arg, uint8_t *data,
                                  size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    DiagnosticLog::printf(
        "[WEB] WebSocket client #%u connected from %s", client->id(),
        client->remoteIP().toString().c_str());
    break;
  case WS_EVT_DISCONNECT:
    DiagnosticLog::printf(
        "[WEB] WebSocket client #%u disconnected", client->id());
    cleanup();
    break;
  case WS_EVT_DATA:
    handleMessage(arg, data, len);
    break;
  case WS_EVT_ERROR:
    cleanup();
    break;
  case WS_EVT_PONG:
  case WS_EVT_PING:
    break;
  }
}

void WebSocketController::handleMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = reinterpret_cast<AwsFrameInfo *>(arg);
  if (!info->final || info->index != 0 || info->len != len ||
      info->opcode != WS_TEXT || len > 192)
    return;

  JsonDocument command;
  if (deserializeJson(command, data, len) != DeserializationError::Ok)
    return;

  const char *type = command["type"] | "";
  if (strcmp(type, "ping") == 0)
    return;
  if (strcmp(type, "setLoad") != 0 ||
      !command["device"].is<uint8_t>() || !command["state"].is<bool>())
    return;

  const uint8_t device = command["device"].as<uint8_t>();
  if (device == 0)
    return;

  ScopedPollingPause pause(_pollingControl);
  _controller.writeLoadState(device, command["state"].as<bool>());
  _mqtt.requestPublish();
}
