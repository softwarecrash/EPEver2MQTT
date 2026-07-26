#include "NetworkManager.h"

NetworkManager::NetworkManager(AsyncWebServer &server, DNSServer &dns,
                               Settings &settings, bool &saveRequested)
    : _server(server),
      _dns(dns),
      _settings(settings),
      _saveRequested(saveRequested)
{
}

bool NetworkManager::connect()
{
  AsyncWiFiManager manager(&_server, &_dns);
  manager.setSaveConfigCallback(
      [this]()
      {
        _saveRequested = true;
        Serial.println(F("Network configuration changed"));
      });

  char mqttPort[7];
  char mqttRefresh[7];
  char deviceQuantity[4];
  snprintf(mqttPort, sizeof(mqttPort), "%u", _settings.data.mqttPort);
  snprintf(mqttRefresh, sizeof(mqttRefresh), "%u",
           _settings.data.mqttRefresh);
  snprintf(deviceQuantity, sizeof(deviceQuantity), "%u",
           _settings.data.deviceQuantity);

  AsyncWiFiManagerParameter mqttServer(
      "mqtt_server", "MQTT server", _settings.data.mqttServer, 40);
  AsyncWiFiManagerParameter mqttUser(
      "mqtt_user", "MQTT User", _settings.data.mqttUser, 40);
  AsyncWiFiManagerParameter mqttPassword(
      "mqtt_pass", "MQTT Password", _settings.data.mqttPassword, 40);
  AsyncWiFiManagerParameter mqttTopic(
      "mqtt_topic", "MQTT Topic", _settings.data.mqttTopic, 40);
  AsyncWiFiManagerParameter mqttPortParameter(
      "mqtt_port", "MQTT Port", mqttPort, sizeof(mqttPort));
  AsyncWiFiManagerParameter mqttRefreshParameter(
      "mqtt_refresh", "MQTT Send Interval", mqttRefresh,
      sizeof(mqttRefresh));
  AsyncWiFiManagerParameter mqttTrigger(
      "mqtt_triggerpath", "MQTT Data Trigger Path",
      _settings.data.mqttTriggerPath, 80);
  AsyncWiFiManagerParameter deviceName(
      "device_name", "Device Name", _settings.data.deviceName, 40);
  AsyncWiFiManagerParameter quantity(
      "device_quantity", "Device Quantity", deviceQuantity,
      sizeof(deviceQuantity));
  AsyncWiFiManagerParameter staticIp(
      "static_ip", "Static IP (empty for DHCP)", _settings.data.staticIP, 16);
  AsyncWiFiManagerParameter staticGateway(
      "static_gw", "Static Gateway (empty for DHCP)",
      _settings.data.staticGW, 16);
  AsyncWiFiManagerParameter staticSubnet(
      "static_sn", "Static Subnet (empty for DHCP)",
      _settings.data.staticSN, 16);
  AsyncWiFiManagerParameter staticDns(
      "static_dns", "Static DNS (empty for DHCP)",
      _settings.data.staticDNS, 16);

  manager.addParameter(&mqttServer);
  manager.addParameter(&mqttUser);
  manager.addParameter(&mqttPassword);
  manager.addParameter(&mqttTopic);
  manager.addParameter(&mqttPortParameter);
  manager.addParameter(&mqttRefreshParameter);
  manager.addParameter(&mqttTrigger);
  manager.addParameter(&deviceName);
  manager.addParameter(&quantity);
  manager.addParameter(&staticIp);
  manager.addParameter(&staticGateway);
  manager.addParameter(&staticSubnet);
  manager.addParameter(&staticDns);

  manager.setDebugOutput(false);
  manager.setMinimumSignalQuality(25);
  manager.setConnectTimeout(10);
  manager.setConfigPortalTimeout(300);
  configureStaticAddress(manager);

  const bool connected = manager.autoConnect("EPEver2MQTT-AP");
  if (!_saveRequested)
    return connected;

  strlcpy(_settings.data.mqttServer, mqttServer.getValue(),
          sizeof(_settings.data.mqttServer));
  strlcpy(_settings.data.mqttUser, mqttUser.getValue(),
          sizeof(_settings.data.mqttUser));
  strlcpy(_settings.data.mqttPassword, mqttPassword.getValue(),
          sizeof(_settings.data.mqttPassword));
  strlcpy(_settings.data.mqttTopic, mqttTopic.getValue(),
          sizeof(_settings.data.mqttTopic));
  strlcpy(_settings.data.mqttTriggerPath, mqttTrigger.getValue(),
          sizeof(_settings.data.mqttTriggerPath));
  strlcpy(_settings.data.deviceName, deviceName.getValue(),
          sizeof(_settings.data.deviceName));
  strlcpy(_settings.data.staticIP, staticIp.getValue(),
          sizeof(_settings.data.staticIP));
  strlcpy(_settings.data.staticGW, staticGateway.getValue(),
          sizeof(_settings.data.staticGW));
  strlcpy(_settings.data.staticSN, staticSubnet.getValue(),
          sizeof(_settings.data.staticSN));
  strlcpy(_settings.data.staticDNS, staticDns.getValue(),
          sizeof(_settings.data.staticDNS));
  _settings.data.mqttPort = atoi(mqttPortParameter.getValue());
  _settings.data.mqttRefresh =
      max(1, atoi(mqttRefreshParameter.getValue()));
  _settings.data.deviceQuantity =
      constrain(atoi(quantity.getValue()), 1, 6);
  _settings.save();
  ESP.restart();
  return connected;
}

void NetworkManager::configureStaticAddress(AsyncWiFiManager &manager)
{
  IPAddress ip;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns;
  if (!ip.fromString(_settings.data.staticIP) ||
      !gateway.fromString(_settings.data.staticGW) ||
      !subnet.fromString(_settings.data.staticSN))
    return;

  dns.fromString(_settings.data.staticDNS);
  manager.setSTAStaticIPConfig(ip, gateway, subnet, dns);
}
