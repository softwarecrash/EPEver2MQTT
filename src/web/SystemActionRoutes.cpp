#include "SystemActionRoutes.h"

#include "../app/DiagnosticLog.h"

#include <AsyncJson.h>
#include <Updater.h>

SystemActionRoutes::SystemActionRoutes(
    AsyncWebServer &server, Settings &settings,
    ApplicationRequests &applicationRequests,
    PollingControl &pollingControl, DeviceAddressService &deviceAddress)
    : _server(server),
      _settings(settings),
      _applicationRequests(applicationRequests),
      _pollingControl(pollingControl),
      _deviceAddress(deviceAddress)
{
}

void SystemActionRoutes::registerRoutes()
{
  _server.on("/api/reset", HTTP_POST,
             [this](AsyncWebServerRequest *request)
             {
               if (!authorize(request))
                 return;
               _applicationRequests.requestFactoryReset();
               request->send(202, "application/json",
                             "{\"ok\":true,\"message\":\"Reset scheduled\"}");
             });

  _server.on("/api/ha-discovery", HTTP_POST,
             [this](AsyncWebServerRequest *request)
             {
               if (!authorize(request))
                 return;
               _applicationRequests.requestDiscovery();
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
          _applicationRequests.requestRestart();
        }
        else
        {
          DiagnosticLog::println("OTA update failed: " +
                                 String(Update.getError()));
          _pollingControl.resume(PollingPauseReason::OtaUpdate);
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
          _pollingControl.pause(PollingPauseReason::OtaUpdate);
          const uint32_t maximumSketchSpace =
              (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
          if (!Update.begin(maximumSketchSpace))
          {
            DiagnosticLog::println("OTA begin failed: " +
                                   String(Update.getError()));
            _pollingControl.resume(PollingPauseReason::OtaUpdate);
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
          _pollingControl.resume(PollingPauseReason::OtaUpdate);
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
  ScopedPollingPause pause(_pollingControl);
  if (_deviceAddress.setAddress(address))
    request->send(200, "application/json",
                  "{\"ok\":true,\"message\":\"Address updated\"}");
  else
    request->send(502, "application/json",
                  "{\"ok\":false,\"message\":\"Controller did not confirm address\"}");
}
