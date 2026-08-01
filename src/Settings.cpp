#include "Settings.h"

#include <EEPROM.h>

#include "ProjectConfig.h"

namespace
{
template <size_t Size>
bool invalidText(const char (&value)[Size])
{
  return strnlen(value, Size) == 0 || strnlen(value, Size) >= Size;
}

template <size_t Size>
void clearText(char (&value)[Size])
{
  value[0] = '\0';
}
}

void Settings::load()
{
  static_assert(sizeof(Data) <= ProjectConfig::EepromSize,
                "Settings do not fit into the reserved EEPROM area");

  EEPROM.begin(ProjectConfig::EepromSize);
  EEPROM.get(0, data);
  EEPROM.end();

  if (data.coVers != ConfigVersion)
  {
    setDefaults();
    save();
    return;
  }

  validate();
}

void Settings::save()
{
  validate();
  data.coVers = ConfigVersion;
  EEPROM.begin(ProjectConfig::EepromSize);
  EEPROM.put(0, data);
  EEPROM.commit();
  EEPROM.end();
}

void Settings::reset()
{
  setDefaults();
  save();
}

void Settings::setDefaults()
{
  data = {};
  data.coVers = ConfigVersion;
  strlcpy(data.deviceName, "EPEver2MQTT", sizeof(data.deviceName));
  strlcpy(data.mqttTopic, "EPEver", sizeof(data.mqttTopic));
  data.mqttRefresh = 300;
  data.deviceQuantity = 1;
  data.LEDBrightness = 127;
  strlcpy(data.NTPServer, "pool.ntp.org", sizeof(data.NTPServer));
}

void Settings::validate()
{
  if (invalidText(data.deviceName))
    strlcpy(data.deviceName, "EPEver2MQTT", sizeof(data.deviceName));
  if (invalidText(data.mqttServer))
    clearText(data.mqttServer);
  if (invalidText(data.mqttUser))
    clearText(data.mqttUser);
  if (invalidText(data.mqttPassword))
    clearText(data.mqttPassword);
  if (invalidText(data.mqttTopic))
    strlcpy(data.mqttTopic, "EPEver", sizeof(data.mqttTopic));
  if (invalidText(data.mqttTriggerPath))
    clearText(data.mqttTriggerPath);
  if (invalidText(data.httpUser))
    clearText(data.httpUser);
  if (invalidText(data.httpPass))
    clearText(data.httpPass);
  if (invalidText(data.NTPTimezone))
    clearText(data.NTPTimezone);
  if (invalidText(data.NTPServer))
    strlcpy(data.NTPServer, "pool.ntp.org", sizeof(data.NTPServer));
  if (invalidText(data.staticIP))
    clearText(data.staticIP);
  if (invalidText(data.staticGW))
    clearText(data.staticGW);
  if (invalidText(data.staticSN))
    clearText(data.staticSN);
  if (invalidText(data.staticDNS))
    clearText(data.staticDNS);

  if (data.mqttPort >= 65530)
    data.mqttPort = 0;
  if (data.mqttRefresh <= 1 || data.mqttRefresh >= 65530)
    data.mqttRefresh = 0;
  if (data.deviceQuantity < 1 ||
      data.deviceQuantity > ProjectConfig::MaximumDevices)
    data.deviceQuantity = 1;
}
