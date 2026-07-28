#include "SystemActionRoutes.h"

#include "../app/DiagnosticLog.h"

#include <AsyncJson.h>
#include <ModbusMaster.h>
#include <Updater.h>

SystemActionRoutes::SystemActionRoutes(
    AsyncWebServer &server, Settings &settings, bool &factoryResetRequested,
    bool &discoveryRequested, bool &workerCanRun, bool &restartRequested,
    unsigned long &restartTimer, HardwareSerial &serial,
    uint8_t transceiverEnablePin)
    : _server(server),
      _settings(settings),
      _factoryResetRequested(factoryResetRequested),
      _discoveryRequested(discoveryRequested),
      _workerCanRun(workerCanRun),
      _restartRequested(restartRequested),
      _restartTimer(restartTimer),
      _serial(serial),
      _transceiverEnablePin(transceiverEnablePin)
{
}

void SystemActionRoutes::registerRoutes()
{
  _server.on("/api/reset", HTTP_POST,
             [this](AsyncWebServerRequest *request)
             {
               if (!authorize(request))
                 return;
               _factoryResetRequested = true;
               request->send(202, "application/json",
                             "{\"ok\":true,\"message\":\"Reset scheduled\"}");
             });

  _server.on("/api/ha-discovery", HTTP_POST,
             [this](AsyncWebServerRequest *request)
             {
               if (!authorize(request))
                 return;
               _discoveryRequested = true;
               request->send(202, "application/json",
                             "{\"ok\":true,\"message\":\"Discovery scheduled\"}");
             });

  auto *addressHandler = new AsyncCallbackJsonWebHandler(
      "/api/device-address",
      [this](AsyncWebServerRequest *request, JsonVariant &json)
      {
        if (!authorize(request))
          return;
        const int address = json["address"] | 0;
        if (address < 1 || address > 247)
        {
          request->send(422, "application/json",
                        "{\"ok\":false,\"message\":\"Address must be 1..247\"}");
          return;
        }
        setDeviceAddress(request, address);
      });
  addressHandler->setMethod(HTTP_POST);
  addressHandler->setMaxContentLength(128);
  _server.addHandler(addressHandler);

  _server.on(
      "/update", HTTP_POST,
      [this](AsyncWebServerRequest *request)
      {
        if (!authorize(request))
          return;
        const bool success = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(
            success ? 200 : 500, "text/plain", success ? "OK" : "FAIL");
        response->addHeader("Connection", "close");
        request->send(response);
        if (success)
        {
          DiagnosticLog::println("OTA update complete; reboot scheduled");
          _restartTimer = millis();
          _restartRequested = true;
        }
        else
        {
          DiagnosticLog::println("OTA update failed: " +
                                 String(Update.getError()));
          _workerCanRun = true;
        }
      },
      [this](AsyncWebServerRequest *request, String filename, size_t index,
             uint8_t *data, size_t len, bool final)
      {
        if (!authorize(request))
          return;
        if (index == 0)
        {
          DiagnosticLog::println("UploadStart: " + filename);
          _workerCanRun = false;
          const uint32_t maximumSketchSpace =
              (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
          if (!Update.begin(maximumSketchSpace))
          {
            DiagnosticLog::println("OTA begin failed: " +
                                   String(Update.getError()));
            _workerCanRun = true;
            return;
          }
          Update.runAsync(true);
        }
        if (Update.write(data, len) != len)
          DiagnosticLog::println("OTA write failed: " +
                                 String(Update.getError()));
        if (final && !Update.end(true))
        {
          DiagnosticLog::println("OTA finalize failed: " +
                                 String(Update.getError()));
          _workerCanRun = true;
        }
      });
}

bool SystemActionRoutes::authorize(AsyncWebServerRequest *request) const
{
  if (strlen(_settings.data.httpUser) == 0 ||
      request->authenticate(_settings.data.httpUser, _settings.data.httpPass))
    return true;
  request->requestAuthentication();
  return false;
}

void SystemActionRoutes::setDeviceAddress(AsyncWebServerRequest *request,
                                          uint8_t address)
{
#ifdef EPEVER_SIMULATION
  request->send(200, "application/json",
                "{\"ok\":true,\"message\":\"Simulated address updated\"}");
  return;
#else
  digitalWrite(_transceiverEnablePin, HIGH);
  delay(50);
  uint8_t frame[8] = {0xF8, 0x45, 0x00, 0x01, 0x01, address, 0, 0};
  uint16_t crc = 0xFFFF;
  for (uint8_t index = 0; index < 6; index++)
    crc = crc16_update(crc, frame[index]);
  frame[6] = lowByte(crc);
  frame[7] = highByte(crc);
  _serial.write(frame, sizeof(frame));
  delay(10);
  digitalWrite(_transceiverEnablePin, LOW);

  uint8_t response[4] = {};
  const size_t received = _serial.readBytes(response, sizeof(response));
  if (received == sizeof(response) && response[2] == address)
    request->send(200, "application/json",
                  "{\"ok\":true,\"message\":\"Address updated\"}");
  else
    request->send(502, "application/json",
                  "{\"ok\":false,\"message\":\"Controller did not confirm address\"}");
#endif
}
