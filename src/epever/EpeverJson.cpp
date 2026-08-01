#include "EpeverController.h"

#include <ESP8266WiFi.h>

namespace
{
const char *const BatteryTypes[] = {
    "User", "Sealed", "GEL", "Flooded", "User"};

const char *const ChargingStates[] = {
    "Off", "Float", "Boost", "Equalization"};

const char *const ChargerInputStates[] = {
    "Normal", "No power connected", "Higher volt input", "Input volt error"};

const char *const NcG3Models[] = {
    "IT5215NC G3",  "IT6215NC G3",  "IT7215NC G3",  "IT10215NC G3",
    "IT5420NC G3",  "IT6415NC G3",  "IT6420NC G3",  "IT7415NC G3",
    "IT7420NC G3",  "IT8420NC G3",  "IT10415NC G3", "IT10420NC G3",
    "ET5215NC G3",  "ET6215NC G3",  "ET7215NC G3",  "ET10215NC G3",
    "ET5420NC G3",  "ET6415NC G3",  "ET6420NC G3",  "ET7415NC G3",
    "ET7420NC G3",  "ET8420NC G3",  "ET10415NC G3", "ET10420NC G3"};

const char *const NcG3BatteryTypes[] = {
    "User", "SLA", "GEL", "Flooded", "LFP4S", "LFP8S", "LFP15S",
    "LFP16S", "LNCM3S", "LNCM6S", "LNCM7S", "LNCM13S", "LNCM14S"};

const char *legacyBatteryVoltageState(uint8_t value)
{
  switch (value)
  {
  case 0:
    return "Normal";
  case 1:
    return "Overvoltage";
  case 2:
    return "Undervoltage";
  case 3:
    return "Low voltage disconnect";
  default:
    return "Fault";
  }
}

const char *legacyBatteryTemperatureState(uint8_t value)
{
  if (value == 0)
    return "Normal";
  if (value == 1)
    return "Over temperature";
  if (value == 2)
    return "Low temperature";
  return "Fault";
}
}

const char *EpeverController::ncG3BatteryVoltageState(uint8_t value)
{
  return legacyBatteryVoltageState(value);
}

const char *EpeverController::ncG3BatteryTemperatureState(uint8_t value)
{
  return legacyBatteryTemperatureState(value);
}

bool EpeverController::updateNcG3Json(uint8_t deviceNumber,
                                      EpeverProfile profile)
{
  char deviceKey[8];
  snprintf(deviceKey, sizeof(deviceKey), "EP_%u", deviceNumber);
  JsonObject device = _liveJson[deviceKey].to<JsonObject>();
  device.clear();
  JsonObject liveData = device["LiveData"].to<JsonObject>();
  JsonObject statsData = device["StatsData"].to<JsonObject>();
  JsonObject deviceData = device["DeviceData"].to<JsonObject>();

  liveData["CONNECTION"] = _errorCode;
  liveData["DEVICE_NUM"] = deviceNumber;
  liveData["DEVICE_TIME"] = currentDeviceTime(deviceNumber);
  liveData["DEVICE_TEMP"] = ncG3.deviceTemperature / 100.f;
  liveData["SOLAR_V"] = ncG3.pv1Voltage / 100.f;
  liveData["SOLAR_A"] = ncG3.pv1Current / 100.f;
  liveData["SOLAR_W"] = ncG3.pv1Power / 100.f;
  if (ncG3.pvCount > 1)
  {
    liveData["SOLAR_2_V"] = ncG3.pv2Voltage / 100.f;
    liveData["SOLAR_2_A"] = ncG3.pv2Current / 100.f;
    liveData["SOLAR_2_W"] = ncG3.pv2Power / 100.f;
  }
  liveData["SOLAR_TOTAL_A"] = ncG3.totalPvCurrent / 100.f;
  liveData["SOLAR_TOTAL_W"] = ncG3.totalPvPower / 100.f;
  liveData["PV_HIGHEST_V"] = ncG3.highestPvVoltage / 100.f;

  liveData["BATT_SOC"] = ncG3.batterySoc;
  liveData["BATT_V"] = ncG3.batteryVoltage / 100.f;
  liveData["BATT_A"] = ncG3.batteryCurrent / 100.f;
  liveData["BATT_W"] =
      (ncG3.batteryVoltage / 100.f) * (ncG3.batteryCurrent / 100.f);
  liveData["BATT_STATE"] =
      ncG3BatteryVoltageState(ncG3.status[0] & 0x0F);
  liveData["BATT_TEMP"] = ncG3.batteryTemperature / 100.f;
  liveData["BATT_TEMP_STATE"] =
      ncG3BatteryTemperatureState((ncG3.status[0] >> 4) & 0x0F);
  liveData["SYSTEM_V"] = ncG3.systemVoltage / 100.f;
  liveData["LITHIUM_VOLTAGE_ID_ERROR"] =
      (ncG3.status[0] & 0x8000) != 0;

  const bool loadAvailable = profile == EpeverProfile::ItNcG3;
  liveData["LOAD_AVAILABLE"] = loadAvailable;
  if (loadAvailable)
  {
    liveData["LOAD_V"] = ncG3.loadVoltage / 100.f;
    liveData["LOAD_A"] = ncG3.loadCurrent / 100.f;
    liveData["LOAD_W"] = ncG3.loadPower / 100.f;
    liveData["LOAD_STATE"] = loadState;
    liveData["LOAD_SHORT_CIRCUIT"] = (ncG3.status[1] & 0x0800) != 0;
    liveData["LOAD_OVERLOAD"] = (ncG3.status[1] >> 12) & 0x03;
  }

  charger_mode = (ncG3.status[2] >> 2) & 0x03;
  charger_input = (ncG3.status[2] >> 14) & 0x03;
  liveData["CHARGER_STATE"] =
      charger_input == 0 ? "Normal" : "Input overvoltage";
  liveData["CHARGER_MODE"] = ChargingStates[charger_mode];
  liveData["DAYTIME"] = (ncG3.status[2] & 0x0002) != 0;
  liveData["DEVICE_OVERHEAT"] = (ncG3.status[2] & 0x0020) != 0;
  liveData["CHARGING_OVERHEAT"] = (ncG3.status[2] & 0x0080) != 0;
  liveData["REMOTE_CHARGING_ENABLED"] =
      (ncG3.status[3] & 0x0100) != 0;
  liveData["LOW_POWER"] = (ncG3.status[3] & 0x0200) != 0;
  liveData["MPPT_ACTIVE"] = (ncG3.status[3] & 0x0020) != 0;
  liveData["PV_MODE_ALARM"] = (ncG3.status[3] >> 10) & 0x03;
  liveData["PV2_INPUT_STATUS"] = (ncG3.status[3] >> 14) & 0x03;

  statsData["BATT_MAX"] = ncG3.batteryMaxToday / 100.f;
  statsData["BATT_MIN"] = ncG3.batteryMinToday / 100.f;
  statsData["CONSUMPTION_AVAILABLE"] = loadAvailable;
  if (loadAvailable)
  {
    statsData["CONS_DAY"] = ncG3.consumedDay / 100.f;
    statsData["CONS_MON"] = ncG3.consumedMonth / 100.f;
    statsData["CONS_YEAR"] = ncG3.consumedYear / 100.f;
    statsData["CONS_TOT"] = ncG3.consumedTotal / 100.f;
  }
  statsData["GEN_DAY"] = ncG3.generatedDay / 100.f;
  statsData["GEN_MON"] = ncG3.generatedMonth / 100.f;
  statsData["GEN_YEAR"] = ncG3.generatedYear / 100.f;
  statsData["GEN_TOT"] = ncG3.generatedTotal / 100.f;

  deviceData["DEVICE_PROFILE"] = epeverProfileName(profile);
  deviceData["DEVICE_MODEL"] =
      ncG3.modelId < 24 ? NcG3Models[ncG3.modelId] : "Unknown NC G3";
  deviceData["MODEL_ID"] = ncG3.modelId;
  deviceData["PV_INPUT_COUNT"] = ncG3.pvCount;
  deviceData["PV_MAX_V"] = ncG3.pvMaxVoltage / 100.f;
  deviceData["RATED_CHARGE_POWER"] = ncG3.ratedChargePower / 100.f;
  deviceData["RATED_BATTERY_V"] = ncG3.ratedBatteryVoltage / 100.f;
  deviceData["RATED_CHARGE_A"] = ncG3.ratedChargeCurrent / 100.f;
  deviceData["RATED_LOAD_A"] = ncG3.ratedLoadCurrent / 100.f;
  deviceData["DSP_FIRMWARE"] = ncG3.dspFirmware / 100.f;
  deviceData["ARM_FIRMWARE"] = ncG3.armFirmware / 100.f;
  deviceData["BATTERY_TYPE"] =
      ncG3.batteryType < 13 ? NcG3BatteryTypes[ncG3.batteryType]
                            : "Unknown";
  deviceData["BATTERY_CAPACITY"] = ncG3.batteryCapacity;
  deviceData["TEMPERATURE_COMPENSATION"] =
      ncG3.temperatureCompensation / -100.f;
  deviceData["HIGH_VOLT_DISCONNECT"] =
      ncG3.highVoltageDisconnect / 100.f;
  deviceData["CHARGING_LIMIT_VOLTS"] =
      ncG3.chargingLimitVoltage / 100.f;
  deviceData["OVER_VOLTS_RECONNECT"] =
      ncG3.overVoltageReconnect / 100.f;
  deviceData["EQUALIZATION_VOLTS"] =
      ncG3.equalizationVoltage / 100.f;
  deviceData["BOOST_VOLTS"] = ncG3.boostVoltage / 100.f;
  deviceData["FLOAT_VOLTS"] = ncG3.floatVoltage / 100.f;
  deviceData["BOOST_RECONNECT_VOLTS"] =
      ncG3.boostReconnectVoltage / 100.f;
  deviceData["LOW_VOLTS_RECONNECT"] =
      ncG3.lowVoltageReconnect / 100.f;
  deviceData["UNDER_VOLTS_RECOVER"] =
      ncG3.underVoltageRecover / 100.f;
  deviceData["UNDER_VOLTS_WARNING"] =
      ncG3.underVoltageWarning / 100.f;
  deviceData["LOW_VOLTS_DISCONNECT"] =
      ncG3.lowVoltageDisconnect / 100.f;
  deviceData["DISCHARGING_LIMIT_VOLTS"] =
      ncG3.dischargingLimitVoltage / 100.f;
  deviceData["CHARGING_CURRENT_LIMIT"] =
      ncG3.chargingCurrentLimit / 100.f;
  deviceData["EQUALIZATION_TIME"] = ncG3.equalizationTime;
  deviceData["BOOST_TIME"] = ncG3.boostTime;
  deviceData["LITHIUM_PROTECTION"] = ncG3.lithiumProtection == 3;
  deviceData["LOW_TEMP_CHARGE_LIMIT"] =
      ncG3.lowTemperatureChargeLimit / 100.f;
  deviceData["LOW_TEMP_DISCHARGE_LIMIT"] =
      ncG3.lowTemperatureDischargeLimit / 100.f;
  deviceData["MAX_BATTERY_TEMP"] =
      ncG3.maximumBatteryTemperature / 100.f;
  deviceData["MIN_BATTERY_TEMP"] =
      ncG3.minimumBatteryTemperature / 100.f;
  deviceData["MAX_DEVICE_TEMP"] =
      ncG3.maximumDeviceTemperature / 100.f;
  deviceData["DEVICE_TEMP_RECOVER"] =
      ncG3.deviceTemperatureRecover / 100.f;
  deviceData["CHARGING_MODE_SETTING"] =
      ncG3.chargingMode == 0 ? "Voltage" : "SOC";
  deviceData["FULL_SOC"] = ncG3.fullSoc;
  deviceData["FULL_SOC_RECOVER"] = ncG3.fullSocRecover;
  deviceData["DISCHARGE_RECOVER_SOC"] = ncG3.dischargeRecoverSoc;
  deviceData["LOW_POWER_RECOVER_SOC"] = ncG3.lowPowerRecoverSoc;
  deviceData["LOW_POWER_ALARM_SOC"] = ncG3.lowPowerAlarmSoc;
  deviceData["DISCHARGE_SOC"] = ncG3.dischargeSoc;
  deviceData["RECORD_PERIOD"] = ncG3.recordPeriod;
  deviceData["BMS_PROTOCOL"] = ncG3.bmsProtocol;
  deviceData["BMS_ENABLED"] = ncG3.bmsEnabled != 0;
  deviceData["PV_INPUT_MODE"] =
      ncG3.pvInputMode == 0 ? "Independent" : "Centralized";
  deviceData["MODBUS_ADDRESS"] = ncG3.modbusAddress;
  deviceData["BAUD_RATE_CODE"] = ncG3.baudRateCode;
  deviceData["PARALLEL_CHARGE_CURRENT_LIMIT"] =
      ncG3.parallelChargeCurrentLimit;

  JsonObject bmsData = device["BmsData"].to<JsonObject>();
  bmsData["ONLINE"] = (ncG3.status[5] & 0x0001) != 0;
  bmsData["LOW_SOC"] = (ncG3.status[5] & 0x0002) != 0;
  bmsData["DISCHARGE_PROTECTION"] = (ncG3.status[5] & 0x0014) != 0;
  bmsData["CHARGE_PROTECTION"] = (ncG3.status[5] & 0x0408) != 0;
  bmsData["SENSOR_FAULT"] = (ncG3.status[5] & 0x0020) != 0;
  bmsData["CELL_LOW_TEMP"] = (ncG3.status[5] & 0x0040) != 0;
  bmsData["CELL_OVER_TEMP"] = (ncG3.status[5] & 0x0080) != 0;
  bmsData["CELL_LOW_VOLTAGE"] = (ncG3.status[5] & 0x0100) != 0;
  bmsData["CELL_OVER_VOLTAGE"] = (ncG3.status[5] & 0x0200) != 0;
  bmsData["FULL_SOC"] = (ncG3.status[5] & 0x2000) != 0;
  bmsData["DSP_COMMUNICATION_FAULT"] =
      (ncG3.status[5] & 0x4000) != 0;
  if (ncG3.bmsDataValid)
  {
    bmsData["CELL_COUNT"] = ncG3.bmsCellCount;
    bmsData["PACK_V"] = ncG3.bmsPackVoltage / 100.f;
    bmsData["PACK_A"] = ncG3.bmsCurrent / 100.f;
    bmsData["FULL_CAPACITY"] = ncG3.bmsFullCapacity;
    bmsData["REMAINING_CAPACITY"] = ncG3.bmsRemainingCapacity;
    bmsData["REMAINING_MINUTES"] = ncG3.bmsRemainingMinutes;
    bmsData["MAX_CELL_TEMP"] =
        ncG3.bmsMaximumCellTemperature / 100.f;
    bmsData["MIN_CELL_TEMP"] =
        ncG3.bmsMinimumCellTemperature / 100.f;
  }
  return true;
}

bool EpeverController::updateJson(uint8_t deviceNumber)
{
  char deviceKey[8];
  snprintf(deviceKey, sizeof(deviceKey), "EP_%u", deviceNumber);
  const EpeverProfile profile =
      deviceNumber <= MaximumDevices
          ? deviceProfiles[deviceNumber]
          : EpeverProfile::Unknown;
  if (isNcG3Profile(profile))
  {
    updateNcG3Json(deviceNumber, profile);
  }
  else
  {
    JsonObject device = _liveJson[deviceKey].to<JsonObject>();
    device.clear();
    JsonObject liveData = device["LiveData"].to<JsonObject>();
    JsonObject statsData = device["StatsData"].to<JsonObject>();
    JsonObject deviceData = device["DeviceData"].to<JsonObject>();

    liveData["CONNECTION"] = _errorCode;
    liveData["DEVICE_NUM"] = deviceNumber;
    liveData["DEVICE_TIME"] = currentDeviceTime(deviceNumber);
    liveData["DEVICE_TEMP"] = deviceTemperature / 100.f;
    liveData["SOLAR_V"] = live.value.pvVoltage / 100.f;
    liveData["SOLAR_A"] = live.value.pvCurrent / 100.f;
    liveData["SOLAR_W"] = live.value.pvPower / 100.f;
    liveData["BATT_SOC"] = batterySOC;
    liveData["BATT_V"] = live.value.batteryVoltage / 100.f;
    liveData["BATT_A"] = batteryCurrent / 100.f;
    liveData["BATT_W"] =
        (live.value.batteryVoltage / 100.f) *
        (batteryCurrent / 100.f);
    liveData["BATT_STATE"] =
        legacyBatteryVoltageState(status_batt.voltage);
    liveData["BATT_TEMP"] = batteryTemperature / 100.f;
    liveData["BATT_TEMP_STATE"] =
        legacyBatteryTemperatureState(status_batt.temperature);
    liveData["LOAD_V"] = live.value.loadVoltage / 100.f;
    liveData["LOAD_A"] = live.value.loadCurrent / 100.f;
    liveData["LOAD_W"] = live.value.loadPower / 100.f;
    liveData["LOAD_STATE"] = loadState;
    liveData["CHARGER_STATE"] =
        charger_input < 4 ? ChargerInputStates[charger_input] : "Unknown";
    liveData["CHARGER_MODE"] =
        charger_mode < 4 ? ChargingStates[charger_mode] : "Unknown";

    statsData["SOLAR_MAX"] = stats.value.pvMaximum / 100.f;
    statsData["SOLAR_MIN"] = stats.value.pvMinimum / 100.f;
    statsData["BATT_MAX"] = stats.value.batteryMaximum / 100.f;
    statsData["BATT_MIN"] = stats.value.batteryMinimum / 100.f;
    statsData["CONS_DAY"] = stats.value.consumedDay / 100.f;
    statsData["CONS_MON"] = stats.value.consumedMonth / 100.f;
    statsData["CONS_YEAR"] = stats.value.consumedYear / 100.f;
    statsData["CONS_TOT"] = stats.value.consumedTotal / 100.f;
    statsData["GEN_DAY"] = stats.value.generatedDay / 100.f;
    statsData["GEN_MON"] = stats.value.generatedMonth / 100.f;
    statsData["GEN_YEAR"] = stats.value.generatedYear / 100.f;
    statsData["GEN_TOT"] = stats.value.generatedTotal / 100.f;

    const BatterySettingRegisters &settings = settingParam;
    deviceData["DEVICE_PROFILE"] = epeverProfileName(profile);
    deviceData["BATTERY_TYPE"] =
        settings.value.batteryType < 5
            ? BatteryTypes[settings.value.batteryType]
            : "Unknown";
    deviceData["BATTERY_CAPACITY"] =
        settings.value.batteryCapacity;
    deviceData["TEMPERATURE_COMPENSATION"] =
        settings.value.temperatureCompensation / 100.f;
    deviceData["HIGH_VOLT_DISCONNECT"] =
        settings.value.highVoltageDisconnect / 100.f;
    deviceData["CHARGING_LIMIT_VOLTS"] =
        settings.value.chargingLimitVoltage / 100.f;
    deviceData["OVER_VOLTS_RECONNECT"] =
        settings.value.overVoltageReconnect / 100.f;
    deviceData["EQUALIZATION_VOLTS"] =
        settings.value.equalizationVoltage / 100.f;
    deviceData["BOOST_VOLTS"] = settings.value.boostVoltage / 100.f;
    deviceData["FLOAT_VOLTS"] = settings.value.floatVoltage / 100.f;
    deviceData["BOOST_RECONNECT_VOLTS"] =
        settings.value.boostReconnectVoltage / 100.f;
    deviceData["LOW_VOLTS_RECONNECT"] =
        settings.value.lowVoltageReconnect / 100.f;
    deviceData["UNDER_VOLTS_RECOVER"] =
        settings.value.underVoltageRecover / 100.f;
    deviceData["UNDER_VOLTS_WARNING"] =
        settings.value.underVoltageWarning / 100.f;
    deviceData["LOW_VOLTS_DISCONNECT"] =
        settings.value.lowVoltageDisconnect / 100.f;
    deviceData["DISCHARGING_LIMIT_VOLTS"] =
        settings.value.dischargingLimitVoltage / 100.f;
  }

  _liveJson["DEVICE_QUANTITY"] = _settings.data.deviceQuantity;
  _liveJson["DEVICE_FREE_HEAP"] = ESP.getFreeHeap();
  _liveJson["ESP_VCC"] = (ESP.getVcc() / 1000.0f) + 0.3f;
  _liveJson["Runtime"] = millis() / 1000UL;
  _liveJson["Wifi_RSSI"] = WiFi.RSSI();
  _liveJson["sw_version"] = _softwareVersion;

  return true;
}
