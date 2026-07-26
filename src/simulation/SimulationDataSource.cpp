#include "SimulationDataSource.h"

#include <math.h>
#include <UnixTime.h>

namespace
{
constexpr float SimulatedDayMilliseconds = 180000.0f;
constexpr uint32_t BaseUnixTime = 1785062400UL;

const char *const Profiles[] = {"", "Tracer AN", "IT6415NC G3",
                                "ET6415NC G3"};
}

SimulationDataSource::SimulationDataSource()
    : _loadState{false, true, true, false},
      _chargeCurrentLimit{0, 30, 60, 60},
      _generatedToday{},
      _consumedToday{},
      _lastUpdate(0),
      _clockOffset(0)
{
  const uint16_t defaults[BatterySettingCount] = {
      0, 200, 300, 1460, 1450, 1380, 1450, 1380,
      1320, 1260, 1260, 1220, 1200, 1160, 1100};
  for (uint8_t device = 1; device <= DeviceCount; device++)
  {
    memcpy(_batterySettings[device], defaults, sizeof(defaults));
    const uint8_t voltageMultiplier = device == 1 ? 1 : (device == 2 ? 2 : 4);
    for (uint8_t index = 3; index < BatterySettingCount; index++)
      _batterySettings[device][index] *= voltageMultiplier;
  }
  _batterySettings[2][0] = 5;
  _batterySettings[3][0] = 7;
}

void SimulationDataSource::begin()
{
  _lastUpdate = millis();
}

void SimulationDataSource::update(JsonDocument &document)
{
  const unsigned long now = millis();
  const float elapsedHours =
      (now - _lastUpdate) / SimulatedDayMilliseconds * 24.0f;
  _lastUpdate = now;

  const float dayProgress = fmodf(now, SimulatedDayMilliseconds) /
                            SimulatedDayMilliseconds;
  const float hours = dayProgress * 24.0f;
  const float sunAngle = (hours - 6.0f) / 12.0f * PI;
  const float clearSky = hours > 6.0f && hours < 18.0f
                             ? fmaxf(0.0f, sinf(sunAngle))
                             : 0.0f;
  const float cloud =
      0.82f + 0.12f * sinf(now / 13700.0f) +
      0.06f * sinf(now / 4100.0f);
  const float solarFactor = clamp(clearSky * cloud, 0.0f, 1.0f);

  for (uint8_t device = 1; device <= DeviceCount; device++)
    addDevice(document, device, solarFactor, hours, elapsedHours);

  document["DEVICE_NAME"] = "EPEver2MQTT-Simulator";
  document["DEVICE_QUANTITY"] = DeviceCount;
  document["DEVICE_FREE_HEAP"] = ESP.getFreeHeap();
  document["ESP_VCC"] = 3.31f;
  document["Runtime"] = millis() / 1000;
  document["Wifi_RSSI"] = -48 + static_cast<int>(4 * sinf(now / 9000.0f));
  document["sw_version"] = SWVERSION;
  document["SIMULATION"] = true;
  document["SIMULATION_HOUR"] = roundf(hours * 100.0f) / 100.0f;
}

void SimulationDataSource::addDevice(JsonDocument &document, uint8_t device,
                                     float solarFactor, float hours,
                                     float elapsedHours)
{
  const float ratedPower[] = {0, 520.0f, 1040.0f, 1040.0f};
  const float batteryNominal[] = {0, 12.8f, 25.6f, 48.0f};
  const float loadBase[] = {0, 36.0f, 92.0f, 140.0f};
  const float pvVoltage[] = {0, 38.0f, 76.0f, 112.0f};

  const float pvPower = ratedPower[device] * solarFactor *
                        (0.96f + 0.03f * sinf(device + millis() / 6000.0f));
  const float loadPower = _loadState[device]
                              ? loadBase[device] *
                                    (1.0f + 0.18f * sinf(hours * 0.8f + device))
                              : 0.0f;
  const float controllerLoss = pvPower * 0.035f;
  const float batteryPower = pvPower - loadPower - controllerLoss;
  const float socWave = 58.0f + 28.0f * sinf((hours - 10.0f) / 24.0f * 2 * PI);
  const float soc = clamp(socWave - device * 3.0f, 18.0f, 98.0f);
  const float batteryVoltage =
      batteryNominal[device] *
      (0.91f + 0.0017f * soc + (batteryPower > 0 ? 0.018f : -0.012f));
  const float batteryCurrent = batteryPower / batteryVoltage;

  _generatedToday[device] +=
      fmaxf(0.0f, pvPower) * elapsedHours / 1000.0f;
  _consumedToday[device] += loadPower * elapsedHours / 1000.0f;

  const String key = "EP_" + String(device);
  JsonObject root = document[key].to<JsonObject>();
  root.clear();
  JsonObject live = root["LiveData"].to<JsonObject>();
  JsonObject stats = root["StatsData"].to<JsonObject>();
  JsonObject data = root["DeviceData"].to<JsonObject>();

  live["CONNECTION"] = 0;
  live["DEVICE_NUM"] = String(device);
  const int64_t simulatedTime =
      static_cast<int64_t>(BaseUnixTime) + _clockOffset +
      static_cast<int64_t>(millis() / SimulatedDayMilliseconds * 86400.0f);
  live["DEVICE_TIME"] = simulatedTime;
  live["DEVICE_TEMP"] = 27.0f + solarFactor * 18.0f + device;
  live["SOLAR_V"] = solarFactor > 0.01f ? pvVoltage[device] : 0.0f;
  live["SOLAR_A"] = solarFactor > 0.01f ? pvPower / pvVoltage[device] : 0.0f;
  live["SOLAR_W"] = pvPower;
  if (device == 3)
  {
    const float pv2Power = pvPower * 0.47f;
    live["SOLAR_2_V"] = live["SOLAR_V"].as<float>() * 0.98f;
    live["SOLAR_2_A"] =
        live["SOLAR_2_V"].as<float>() > 0
            ? pv2Power / live["SOLAR_2_V"].as<float>()
            : 0;
    live["SOLAR_2_W"] = pv2Power;
  }
  live["SOLAR_TOTAL_A"] = live["SOLAR_A"];
  live["SOLAR_TOTAL_W"] = pvPower;
  live["PV_HIGHEST_V"] = pvVoltage[device] * 1.08f;
  live["BATT_SOC"] = roundf(soc);
  live["BATT_V"] = batteryVoltage;
  live["BATT_A"] = batteryCurrent;
  live["BATT_W"] = batteryPower;
  live["BATT_STATE"] = "Normal";
  live["BATT_TEMP"] = 23.5f + solarFactor * 5.0f;
  live["BATT_TEMP_STATE"] = "Normal";
  live["SYSTEM_V"] = batteryNominal[device];
  live["LOAD_AVAILABLE"] = device != 3;
  if (device != 3)
  {
    live["LOAD_V"] = batteryVoltage;
    live["LOAD_A"] = batteryVoltage > 0 ? loadPower / batteryVoltage : 0;
    live["LOAD_W"] = loadPower;
    live["LOAD_STATE"] = _loadState[device];
  }
  live["CHARGER_STATE"] = "Normal";
  live["CHARGER_MODE"] =
      solarFactor < 0.02f ? "Off" : (soc > 94 ? "Float" : "Boost");
  live["DAYTIME"] = solarFactor > 0;
  live["MPPT_ACTIVE"] = solarFactor > 0.03f;

  stats["SOLAR_MAX"] = pvVoltage[device] * 1.08f;
  stats["SOLAR_MIN"] = 0;
  stats["BATT_MAX"] = batteryNominal[device] * 1.14f;
  stats["BATT_MIN"] = batteryNominal[device] * 0.91f;
  stats["CONSUMPTION_AVAILABLE"] = device != 3;
  stats["CONS_DAY"] = _consumedToday[device];
  stats["CONS_MON"] = 62.4f + _consumedToday[device];
  stats["CONS_YEAR"] = 734.2f + _consumedToday[device];
  stats["CONS_TOT"] = 2841.7f + _consumedToday[device];
  stats["GEN_DAY"] = _generatedToday[device];
  stats["GEN_MON"] = 91.8f + _generatedToday[device];
  stats["GEN_YEAR"] = 1087.4f + _generatedToday[device];
  stats["GEN_TOT"] = 4926.3f + _generatedToday[device];

  data["DEVICE_PROFILE"] = Profiles[device];
  data["DEVICE_MODEL"] = device == 1 ? "Tracer4210AN"
                                      : (device == 2 ? "IT6415NC G3"
                                                     : "ET6415NC G3");
  data["MODEL_ID"] = device == 1 ? 100 : (device == 2 ? 5 : 17);
  data["PV_INPUT_COUNT"] = device == 3 ? 2 : 1;
  data["PV_MAX_V"] = pvVoltage[device] * 1.32f;
  data["RATED_CHARGE_POWER"] = ratedPower[device];
  data["RATED_BATTERY_V"] = batteryNominal[device];
  data["RATED_CHARGE_A"] = device == 1 ? 40 : 60;
  data["RATED_LOAD_A"] = device == 3 ? 0 : 30;
  data["BATTERY_TYPE"] = device == 1 ? "User" : "LFP8S";
  data["BATTERY_CAPACITY"] = device == 1 ? 200 : 280;
  data["CHARGING_CURRENT_LIMIT"] = _chargeCurrentLimit[device];
  data["HIGH_VOLT_DISCONNECT"] = _batterySettings[device][3] / 100.0f;
  data["CHARGING_LIMIT_VOLTS"] = _batterySettings[device][4] / 100.0f;
  data["OVER_VOLTS_RECONNECT"] = _batterySettings[device][5] / 100.0f;
  data["EQUALIZATION_VOLTS"] = _batterySettings[device][6] / 100.0f;
  data["BOOST_VOLTS"] = _batterySettings[device][7] / 100.0f;
  data["FLOAT_VOLTS"] = _batterySettings[device][8] / 100.0f;
  data["BOOST_RECONNECT_VOLTS"] = _batterySettings[device][9] / 100.0f;
  data["LOW_VOLTS_RECONNECT"] = _batterySettings[device][10] / 100.0f;
  data["UNDER_VOLTS_RECOVER"] = _batterySettings[device][11] / 100.0f;
  data["UNDER_VOLTS_WARNING"] = _batterySettings[device][12] / 100.0f;
  data["LOW_VOLTS_DISCONNECT"] = _batterySettings[device][13] / 100.0f;
  data["DISCHARGING_LIMIT_VOLTS"] = _batterySettings[device][14] / 100.0f;

  if (device == 2)
  {
    JsonObject bms = root["BmsData"].to<JsonObject>();
    bms["ONLINE"] = true;
    bms["LOW_SOC"] = soc < 20;
    bms["DISCHARGE_PROTECTION"] = false;
    bms["CHARGE_PROTECTION"] = false;
    bms["SENSOR_FAULT"] = false;
    bms["CELL_LOW_TEMP"] = false;
    bms["CELL_OVER_TEMP"] = false;
    bms["CELL_LOW_VOLTAGE"] = false;
    bms["CELL_OVER_VOLTAGE"] = false;
    bms["FULL_SOC"] = soc > 98;
    bms["CELL_COUNT"] = 8;
    bms["PACK_V"] = batteryVoltage;
    bms["PACK_A"] = batteryCurrent;
    bms["FULL_CAPACITY"] = 280;
    bms["REMAINING_CAPACITY"] = roundf(280.0f * soc / 100.0f);
    bms["REMAINING_MINUTES"] =
        batteryCurrent < -0.1f
            ? roundf(bms["REMAINING_CAPACITY"].as<float>() /
                     -batteryCurrent * 60.0f)
            : 0;
    bms["MAX_CELL_TEMP"] = 27.1f + solarFactor * 2.0f;
    bms["MIN_CELL_TEMP"] = 25.8f + solarFactor * 1.5f;
  }
}

EpeverProfile SimulationDataSource::profile(uint8_t device) const
{
  if (device == 1)
    return EpeverProfile::Legacy;
  if (device == 2)
    return EpeverProfile::ItNcG3;
  if (device == 3)
    return EpeverProfile::EtNcG3;
  return EpeverProfile::Unknown;
}

bool SimulationDataSource::setLoadState(uint8_t device, bool state)
{
  if (device == 0 || device > DeviceCount || device == 3)
    return false;
  _loadState[device] = state;
  return true;
}

bool SimulationDataSource::setChargeCurrentLimit(uint8_t device, float amps)
{
  if (device < 2 || device > DeviceCount || amps <= 0 || amps > 60)
    return false;
  _chargeCurrentLimit[device] = amps;
  return true;
}

bool SimulationDataSource::readBatterySettings(uint8_t device,
                                               uint16_t *values) const
{
  if (device == 0 || device > DeviceCount || values == nullptr)
    return false;
  memcpy(values, _batterySettings[device],
         sizeof(_batterySettings[device]));
  return true;
}

bool SimulationDataSource::writeBatterySettings(uint8_t device,
                                                const uint16_t *values)
{
  if (device == 0 || device > DeviceCount || values == nullptr)
    return false;
  memcpy(_batterySettings[device], values,
         sizeof(_batterySettings[device]));
  return true;
}

bool SimulationDataSource::setClock(const char *dateTime)
{
  if (dateTime == nullptr || strlen(dateTime) != 12)
    return false;
  uint8_t parts[6];
  for (uint8_t index = 0; index < 6; index++)
  {
    if (!isDigit(dateTime[index * 2]) || !isDigit(dateTime[index * 2 + 1]))
      return false;
    parts[index] = (dateTime[index * 2] - '0') * 10 +
                   dateTime[index * 2 + 1] - '0';
  }
  UnixTime targetTime(0);
  targetTime.setDateTime(2000 + parts[0], parts[1], parts[2],
                         parts[3], parts[4], parts[5]);
  const int64_t currentSimulatedTime =
      BaseUnixTime +
      static_cast<uint32_t>(millis() / SimulatedDayMilliseconds * 86400);
  _clockOffset = static_cast<int32_t>(
      static_cast<int64_t>(targetTime.getUnix()) - currentSimulatedTime);
  return true;
}

float SimulationDataSource::clamp(float value, float minimum, float maximum)
{
  return fmaxf(minimum, fminf(maximum, value));
}
