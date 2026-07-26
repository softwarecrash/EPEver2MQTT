#include "EpeverController.h"

#include <ESP8266WiFi.h>
#include <math.h>

#define MAX_DEVICES 6
namespace
{
#include "../epregister.h"
}
#undef MAX_DEVICES

#ifdef EPEVER_SIMULATION
#include "../simulation/SimulationDataSource.h"
#endif

EpeverController::EpeverController(
    ModbusMaster &modbus, JsonDocument &liveJson, Settings &settings,
    UnixTime &unixTime, DallasTemperature &temperatureSensors,
    uint8_t &temperatureSensorCount, uint8_t *temperatureAddress,
    int &errorCode, const char *softwareVersion)
    : _modbus(modbus),
      _liveJson(liveJson),
      _settings(settings),
      _unixTime(unixTime),
      _temperatureSensors(temperatureSensors),
      _temperatureSensorCount(temperatureSensorCount),
      _temperatureAddress(temperatureAddress),
      _errorCode(errorCode),
      _softwareVersion(softwareVersion)
{
}

#ifdef EPEVER_SIMULATION
EpeverController::EpeverController(
    ModbusMaster &modbus, JsonDocument &liveJson, Settings &settings,
    UnixTime &unixTime, DallasTemperature &temperatureSensors,
    uint8_t &temperatureSensorCount, uint8_t *temperatureAddress,
    int &errorCode, const char *softwareVersion,
    SimulationDataSource &simulation)
    : EpeverController(modbus, liveJson, settings, unixTime,
                       temperatureSensors, temperatureSensorCount,
                       temperatureAddress, errorCode, softwareVersion)
{
  _simulation = &simulation;
}
#endif
uint32_t EpeverController::wordsToUint32(uint16_t lowWord, uint16_t highWord)
{
  return (uint32_t)lowWord | ((uint32_t)highWord << 16);
}

bool EpeverController::readInputBlock(uint16_t address, uint8_t count, uint16_t *values)
{
  _modbus.clearResponseBuffer();
  result = _modbus.readInputRegisters(address, count);
  if (result != _modbus.ku8MBSuccess)
    return false;

  for (uint8_t index = 0; index < count; index++)
    values[index] = _modbus.getResponseBuffer(index);
  return true;
}

bool EpeverController::readHoldingBlock(uint16_t address, uint8_t count, uint16_t *values)
{
  _modbus.clearResponseBuffer();
  result = _modbus.readHoldingRegisters(address, count);
  if (result != _modbus.ku8MBSuccess)
    return false;

  for (uint8_t index = 0; index < count; index++)
    values[index] = _modbus.getResponseBuffer(index);
  return true;
}

EpeverProfile EpeverController::detectProfile(uint8_t device, bool force)
{
#ifdef EPEVER_SIMULATION
  (void)force;
  EpeverProfile simulatedProfile = _simulation->profile(device);
  if (device <= MaximumDevices)
  {
    deviceProfiles[device] = simulatedProfile;
    deviceModelIds[device] =
        device == 2 ? 5 : (device == 3 ? 17 : 100);
    deviceRatedChargeCurrent[device] = device == 1 ? 4000 : 6000;
  }
  return simulatedProfile;
#endif
  if (device == 0 || device > MaximumDevices)
    return EpeverProfile::Unknown;
  if (!force && deviceProfiles[device] != EpeverProfile::Unknown)
    return deviceProfiles[device];

  _modbus.setSlaveId(device);
  uint16_t model;
  if (!readInputBlock(NC_G3_MODEL, 1, &model))
    return EpeverProfile::Unknown;

  if (model <= 11)
    deviceProfiles[device] = EpeverProfile::ItNcG3;
  else if (model <= 23)
    deviceProfiles[device] = EpeverProfile::EtNcG3;
  else
    deviceProfiles[device] = EpeverProfile::Legacy;

  if (model <= 23)
    deviceModelIds[device] = model;
  return deviceProfiles[device];
}

bool EpeverController::readNcG3(uint8_t invNum, EpeverProfile profile)
{
  memset(&ncG3, 0, sizeof(ncG3));
  memset(rtc.buf, 0, sizeof(rtc.buf));
  _unixTime.setDateTime(0, 0, 0, 0, 0, 0);
  loadState = false;

  uint16_t values[20];
  ncG3.modelId = deviceModelIds[invNum];

  // Rated data. Only the model and charge-current rating are required for
  // profile selection and safe current-limit writes; other ratings are
  // optional because firmware revisions expose slightly different subsets.
  if (readInputBlock(0x3002, 1, values))
    ncG3.pvMaxVoltage = values[0];
  if (readInputBlock(0x3004, 4, values))
  {
    ncG3.ratedChargePower = wordsToUint32(values[0], values[1]);
    ncG3.ratedBatteryVoltage = values[2];
    ncG3.ratedChargeCurrent = values[3];
    deviceRatedChargeCurrent[invNum] = values[3];
  }
  if (profile == EpeverProfile::ItNcG3 && readInputBlock(0x300B, 1, values))
    ncG3.ratedLoadCurrent = values[0];
  if (readInputBlock(0x300E, 2, values))
  {
    ncG3.dspFirmware = values[0];
    ncG3.pvCount = values[1];
  }
  if (readInputBlock(0x3011, 1, values))
    ncG3.armFirmware = values[0];
  if (ncG3.pvCount < 1 || ncG3.pvCount > 2)
    ncG3.pvCount = 1;

  // Core live data.
  if (!readInputBlock(0x3100, 4, values))
    goto read_failed;
  ncG3.pv1Voltage = values[0];
  ncG3.pv1Current = values[1];
  ncG3.pv1Power = wordsToUint32(values[2], values[3]);

  if (ncG3.pvCount > 1 && readInputBlock(0x3108, 4, values))
  {
    ncG3.pv2Voltage = values[0];
    ncG3.pv2Current = values[1];
    ncG3.pv2Power = wordsToUint32(values[2], values[3]);
  }

  if (profile == EpeverProfile::ItNcG3)
  {
    if (!readInputBlock(0x3110, 4, values))
      goto read_failed;
    ncG3.loadVoltage = values[0];
    ncG3.loadCurrent = values[1];
    ncG3.loadPower = wordsToUint32(values[2], values[3]);
  }

  if (!readInputBlock(0x3114, 1, values))
    goto read_failed;
  ncG3.batteryVoltage = values[0];
  if (!readInputBlock(0x3117, 4, values))
    goto read_failed;
  ncG3.batteryCurrent = (int16_t)values[0];
  ncG3.batteryTemperature = (int16_t)values[1];
  ncG3.batterySoc = values[2];
  ncG3.deviceTemperature = (int16_t)values[3];
  if (!readInputBlock(0x311D, 5, values))
    goto read_failed;
  ncG3.systemVoltage = values[0];
  ncG3.highestPvVoltage = values[1];
  ncG3.totalPvCurrent = values[2];
  ncG3.totalPvPower = wordsToUint32(values[3], values[4]);

  if (!readInputBlock(0x3200, 4, values))
    goto read_failed;
  for (uint8_t index = 0; index < 4; index++)
    ncG3.status[index] = values[index];
  if (readInputBlock(0x3205, 1, values))
    ncG3.status[5] = values[0];

  if (!readInputBlock(0x3301, 2, values))
    goto read_failed;
  ncG3.batteryMaxToday = values[0];
  ncG3.batteryMinToday = values[1];

  if (profile == EpeverProfile::ItNcG3)
  {
    if (!readInputBlock(0x3303, 16, values))
      goto read_failed;
    ncG3.consumedDay = wordsToUint32(values[0], values[1]);
    ncG3.consumedMonth = wordsToUint32(values[2], values[3]);
    ncG3.consumedYear = wordsToUint32(values[4], values[5]);
    ncG3.consumedTotal = wordsToUint32(values[6], values[7]);
    ncG3.generatedDay = wordsToUint32(values[8], values[9]);
    ncG3.generatedMonth = wordsToUint32(values[10], values[11]);
    ncG3.generatedYear = wordsToUint32(values[12], values[13]);
    ncG3.generatedTotal = wordsToUint32(values[14], values[15]);
  }
  else
  {
    if (!readInputBlock(0x330B, 8, values))
      goto read_failed;
    ncG3.generatedDay = wordsToUint32(values[0], values[1]);
    ncG3.generatedMonth = wordsToUint32(values[2], values[3]);
    ncG3.generatedYear = wordsToUint32(values[4], values[5]);
    ncG3.generatedTotal = wordsToUint32(values[6], values[7]);
  }

  // Main battery settings, split around gaps in the G3 map.
  if (!readHoldingBlock(0x9000, 3, values))
    goto read_failed;
  ncG3.batteryType = values[0];
  ncG3.batteryCapacity = values[1];
  ncG3.temperatureCompensation = values[2];
  if (!readHoldingBlock(0x9007, 13, values))
    goto read_failed;
  ncG3.highVoltageDisconnect = values[0];
  ncG3.chargingLimitVoltage = values[1];
  ncG3.overVoltageReconnect = values[2];
  ncG3.equalizationVoltage = values[3];
  ncG3.boostVoltage = values[4];
  ncG3.floatVoltage = values[5];
  ncG3.boostReconnectVoltage = values[6];
  ncG3.lowVoltageReconnect = values[7];
  ncG3.underVoltageRecover = values[8];
  ncG3.underVoltageWarning = values[9];
  ncG3.lowVoltageDisconnect = values[10];
  ncG3.dischargingLimitVoltage = values[11];
  ncG3.chargingCurrentLimit = values[12];

  // Optional settings and the G3 RTC. Not all firmware exposes every group.
  if (readHoldingBlock(0x9014, 9, values))
  {
    ncG3.equalizationTime = values[0];
    ncG3.boostTime = values[1];
    ncG3.lithiumProtection = values[2];
    ncG3.lowTemperatureChargeLimit = (int16_t)values[3];
    ncG3.lowTemperatureDischargeLimit = (int16_t)values[4];
    rtc.buf[0] = values[5];
    rtc.buf[1] = values[6];
    rtc.buf[2] = values[7];
    ncG3.maximumBatteryTemperature = (int16_t)values[8];
    _unixTime.setDateTime(2000 + rtc.r.y, rtc.r.M, rtc.r.d, rtc.r.h, rtc.r.m, rtc.r.s);
  }
  if (readHoldingBlock(0x901D, 3, values))
  {
    ncG3.minimumBatteryTemperature = (int16_t)values[0];
    ncG3.maximumDeviceTemperature = (int16_t)values[1];
    ncG3.deviceTemperatureRecover = (int16_t)values[2];
  }
  if (readHoldingBlock(0x9038, 16, values))
  {
    ncG3.chargingMode = values[0];
    ncG3.fullSoc = values[1];
    ncG3.fullSocRecover = values[2];
    ncG3.dischargeRecoverSoc = values[3];
    ncG3.lowPowerRecoverSoc = values[4];
    ncG3.lowPowerAlarmSoc = values[5];
    ncG3.dischargeSoc = values[6];
    ncG3.recordPeriod = values[7];
    ncG3.bmsProtocol = values[8];
    ncG3.bmsEnabled = values[9];
    ncG3.pvInputMode = values[10];
    ncG3.modbusAddress = values[13];
    ncG3.baudRateCode = values[14];
    ncG3.parallelChargeCurrentLimit = values[15];
  }

  // The IT profile exposes detailed BMS telemetry. Keep this optional so a
  // controller without an active BMS still publishes its normal live data.
  if (profile == EpeverProfile::ItNcG3 && readInputBlock(0x3400, 10, values))
  {
    ncG3.bmsCellCount = values[0];
    ncG3.bmsPackVoltage = values[1];
    ncG3.bmsCurrent = (int16_t)values[2];
    ncG3.bmsFullCapacity = values[5];
    ncG3.bmsRemainingCapacity = values[6];
    ncG3.bmsRemainingMinutes = values[7];
    ncG3.bmsMaximumCellTemperature = (int16_t)values[8];
    ncG3.bmsMinimumCellTemperature = (int16_t)values[9];
    ncG3.bmsDataValid = true;
  }

  if (profile == EpeverProfile::ItNcG3)
  {
    _modbus.clearResponseBuffer();
    result = _modbus.readCoils(NC_G3_LOAD_STATE, 1);
    loadState = result == _modbus.ku8MBSuccess
                    ? _modbus.getResponseBuffer(0) != 0
                    : (ncG3.status[1] & 0x0001) != 0;
  }

  _errorCode = 0;
  Serial.println("[" + String(invNum) + "] NC G3 transmission OK.");
  return true;

read_failed:
  _errorCode = result;
  Serial.println("[" + String(invNum) + "] " + String(result) + " NC G3 register read failed");
  return false;
}

bool EpeverController::read(uint8_t invNum)
{
#ifdef EPEVER_SIMULATION
  _errorCode = 0;
  return detectProfile(invNum) != EpeverProfile::Unknown;
#endif
  _errorCode = 0;
  _modbus.setSlaveId(invNum);
  EpeverProfile profile = detectProfile(invNum);

  if (profile == EpeverProfile::ItNcG3 || profile == EpeverProfile::EtNcG3)
    return readNcG3(invNum, profile);

  return readLegacy(invNum);
}

bool EpeverController::readLegacy(uint8_t invNum)
{
  _errorCode = 0;
  _modbus.setSlaveId(invNum);

  // clear buffers
  memset(rtc.buf, 0, sizeof(rtc.buf));
  memset(live.buf, 0, sizeof(live.buf));
  memset(stats.buf, 0, sizeof(stats.buf));
  batteryCurrent = 0;
  batterySOC = 0;
  _unixTime.setDateTime(0, 0, 0, 0, 0, 0);

  _modbus.clearResponseBuffer();
  result = _modbus.readHoldingRegisters(RTC_CLOCK, RTC_CLOCK_CNT);
  if (result == _modbus.ku8MBSuccess)
  {
    rtc.buf[0] = _modbus.getResponseBuffer(0);
    rtc.buf[1] = _modbus.getResponseBuffer(1);
    rtc.buf[2] = _modbus.getResponseBuffer(2);
    _unixTime.setDateTime(2000 + rtc.r.y, rtc.r.M, rtc.r.d,
                          rtc.r.h, rtc.r.m, rtc.r.s);

    _errorCode = result;
  }
  else
  {
    Serial.println("[" + String(invNum) + "] " + result + " Read registers for clock Failed");
    _errorCode += result;
    return false;
  }
  // read LIVE-Data 0x3100
  _modbus.clearResponseBuffer();
  result = _modbus.readInputRegisters(LIVE_DATA, LIVE_DATA_CNT);
  if (result == _modbus.ku8MBSuccess)
  {

    for (i = 0; i < LIVE_DATA_CNT; i++)
      live.buf[i] = _modbus.getResponseBuffer(i);

    _errorCode += result;
  }
  else
  {
    Serial.println("[" + String(invNum) + "] " + result + " Read LIVE-Dat Failed");
    _errorCode += result;
    return false;
  }
  // Statistical Data 0x3300
  _modbus.clearResponseBuffer();
  result = _modbus.readInputRegisters(STATISTICS, STATISTICS_CNT);
  if (result == _modbus.ku8MBSuccess)
  {
    for (i = 0; i < STATISTICS_CNT; i++)
      stats.buf[i] = _modbus.getResponseBuffer(i);

    _errorCode += result;
  }
  else
  {
    Serial.println("[" + String(invNum) + "] " + result + " Read Statistical Data Failed");
    _errorCode += result;
    return false;
  }

  _modbus.clearResponseBuffer();
  result = _modbus.readInputRegisters(BATTERY_SOC, 1);
  if (result == _modbus.ku8MBSuccess)
  {
    batterySOC = _modbus.getResponseBuffer(0);
    _errorCode += result;
  }
  else
  {
    Serial.println("[" + String(invNum) + "] " + result + " Read Battery SOC Failed");
    _errorCode += result;
    return false;
  }
  // Battery Net Current = Icharge - Iload
  _modbus.clearResponseBuffer();
  result = _modbus.readInputRegisters(BATTERY_CURRENT_L, 2);
  if (result == _modbus.ku8MBSuccess)
  {
    batteryCurrent = _modbus.getResponseBuffer(0);
    batteryCurrent |= _modbus.getResponseBuffer(1) << 16;
    _errorCode += result;
  }
  else
  {
    Serial.println("[" + String(invNum) + "] " + result + " Read Battery Net Current = Icharge - Iload Failed");
    _errorCode += result;
    return false;
  }
  // State of the Load Switch
  _modbus.clearResponseBuffer();
  result = _modbus.readCoils(LOAD_STATE, 1);
  if (result == _modbus.ku8MBSuccess)
  {
    loadState = _modbus.getResponseBuffer(0) ? true : false;
    _errorCode += result;
  }
  else
  {
    Serial.println("[" + String(invNum) + "] " + result + " Read State of the Load Switch Failed");
    _errorCode += result;
    return false;
  }
  // Read Status Flags
  _modbus.clearResponseBuffer();
  result = _modbus.readInputRegisters(0x3200, 2);
  if (result == _modbus.ku8MBSuccess)
  {
    uint16_t temp = _modbus.getResponseBuffer(0);
    status_batt.volt = temp & 0b1111;
    status_batt.temp = (temp >> 4) & 0b1111;
    status_batt.resistance = (temp >> 8) & 0b1;
    status_batt.rated_volt = (temp >> 15) & 0b1;

    temp = _modbus.getResponseBuffer(1);


    // charger_input     = ( temp & 0b0000000000000000 ) >> 15 ;
    charger_mode = (temp & 0b0000000000001100) >> 2;
    // charger_input     = ( temp & 0b0000000000000000 ) >> 12 ;
    _errorCode += result;
  }
  else
  {
    Serial.println("[" + String(invNum) + "] " + result + " Read Read Status Flags Failed");
    _errorCode += result;
    return false;
  }

  _modbus.clearResponseBuffer();
  result = _modbus.readInputRegisters(DEVICE_TEMPERATURE, 1);
  if (result == _modbus.ku8MBSuccess)
  {
    deviceTemperature = _modbus.getResponseBuffer(0);
    _errorCode += result;
  }
  else
  {
    Serial.println("[" + String(invNum) + "] " + result + " Read Device Temperature Failed");
    _errorCode += result;
    return false;
  }

  _modbus.clearResponseBuffer();
  result = _modbus.readInputRegisters(BATTERY_TEMPERATURE, 1);
  if (result == _modbus.ku8MBSuccess)
  {
    batteryTemperature = _modbus.getResponseBuffer(0);
    _errorCode += result;
  }
  else
  {
    Serial.println("[" + String(invNum) + "] " + result + " Read Battery temperature Failed");
    _errorCode += result;
    return false;
  }

  _modbus.clearResponseBuffer();
  result = _modbus.readHoldingRegisters(DEVICE_SETTINGS, DEVICE_SETTINGS_CNT);
  if (result == _modbus.ku8MBSuccess)
  {
    for (i = 0; i < DEVICE_SETTINGS_CNT; i++)
      settingParam.buf[i] = _modbus.getResponseBuffer(i);
    _errorCode += result;
  }
  else
  {
    Serial.println("[" + String(invNum) + "] " + result + " Read Settings Data Failed");
    _errorCode += result;
    return false;
  }
  if (_errorCode == 0)
  {
    Serial.println("[" + String(invNum) + "] Transmission OK.");
  }
  return true;
}

bool EpeverController::writeLoadState(uint8_t device, bool state)
{
#ifdef EPEVER_SIMULATION
  return _simulation->setLoadState(device, state);
#endif
  EpeverProfile profile = detectProfile(device);
  if (profile == EpeverProfile::Unknown || profile == EpeverProfile::EtNcG3)
    return false;

  _modbus.setSlaveId(device);
  uint16_t coil = profile == EpeverProfile::ItNcG3 ? NC_G3_LOAD_STATE : LOAD_STATE;
  uint8_t writeStatus = _modbus.writeSingleCoil(coil, state ? 1 : 0);
  delay(50);
  _modbus.clearResponseBuffer();
  uint8_t readStatus = _modbus.readCoils(coil, 1);
  return writeStatus == _modbus.ku8MBSuccess &&
         readStatus == _modbus.ku8MBSuccess &&
         (_modbus.getResponseBuffer(0) != 0) == state;
}

bool EpeverController::writeChargeCurrentLimit(uint8_t device, float amps)
{
#ifdef EPEVER_SIMULATION
  return _simulation->setChargeCurrentLimit(device, amps);
#endif
  if (!isfinite(amps) || amps <= 0)
    return false;

  EpeverProfile profile = detectProfile(device);
  if (profile != EpeverProfile::ItNcG3 && profile != EpeverProfile::EtNcG3)
    return false;

  _modbus.setSlaveId(device);
  uint16_t ratedRaw = deviceRatedChargeCurrent[device];
  if (ratedRaw == 0 && !readInputBlock(NC_G3_RATED_CHARGE_CURRENT, 1, &ratedRaw))
    return false;
  deviceRatedChargeCurrent[device] = ratedRaw;

  float scaled = amps * 100.0f;
  uint32_t requestedRaw = (uint32_t)roundf(scaled);
  if (requestedRaw == 0 || requestedRaw > ratedRaw || requestedRaw > UINT16_MAX ||
      fabsf(scaled - requestedRaw) > 0.01f)
    return false;

  uint8_t writeStatus = _modbus.writeSingleRegister(NC_G3_CHARGE_CURRENT_LIMIT, (uint16_t)requestedRaw);
  delay(100);
  uint16_t readBack;
  bool verified = readHoldingBlock(NC_G3_CHARGE_CURRENT_LIMIT, 1, &readBack) &&
                  readBack == requestedRaw;
  if (verified)
    ncG3.chargingCurrentLimit = readBack;
  return writeStatus == _modbus.ku8MBSuccess && verified;
}

uint16_t EpeverController::ratedChargeCurrent(uint8_t device) const
{
  return device <= MaximumDevices ? deviceRatedChargeCurrent[device] : 0;
}

const char *EpeverController::ncG3BatteryVoltageState(uint8_t value)
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

const char *EpeverController::ncG3BatteryTemperatureState(uint8_t value)
{
  if (value == 0)
    return "Normal";
  if (value == 1)
    return "Over temperature";
  if (value == 2)
    return "Low temperature";
  return "Fault";
}

bool EpeverController::updateNcG3Json(uint8_t invNum, EpeverProfile profile)
{
  String deviceKey = "EP_" + String(invNum);
  JsonObject device = _liveJson[deviceKey].to<JsonObject>();
  device.clear();
  JsonObject liveData = device["LiveData"].to<JsonObject>();
  JsonObject statsData = device["StatsData"].to<JsonObject>();
  JsonObject deviceData = device["DeviceData"].to<JsonObject>();

  liveData["CONNECTION"] = _errorCode;
  liveData["DEVICE_NUM"] = String(invNum);
  liveData["DEVICE_TIME"] = _unixTime.getUnix();
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
  liveData["BATT_W"] = (ncG3.batteryVoltage / 100.f) * (ncG3.batteryCurrent / 100.f);
  liveData["BATT_STATE"] = ncG3BatteryVoltageState(ncG3.status[0] & 0x0f);
  liveData["BATT_TEMP"] = ncG3.batteryTemperature / 100.f;
  liveData["BATT_TEMP_STATE"] = ncG3BatteryTemperatureState((ncG3.status[0] >> 4) & 0x0f);
  liveData["SYSTEM_V"] = ncG3.systemVoltage / 100.f;
  liveData["LITHIUM_VOLTAGE_ID_ERROR"] = (ncG3.status[0] & 0x8000) != 0;

  bool loadAvailable = profile == EpeverProfile::ItNcG3;
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
  liveData["CHARGER_STATE"] = charger_input == 0 ? "Normal" : "Input overvoltage";
  liveData["CHARGER_MODE"] = charger_charging_status[charger_mode];
  liveData["DAYTIME"] = (ncG3.status[2] & 0x0002) != 0;
  liveData["DEVICE_OVERHEAT"] = (ncG3.status[2] & 0x0020) != 0;
  liveData["CHARGING_OVERHEAT"] = (ncG3.status[2] & 0x0080) != 0;
  liveData["REMOTE_CHARGING_ENABLED"] = (ncG3.status[3] & 0x0100) != 0;
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

  deviceData["DEVICE_PROFILE"] = profile == EpeverProfile::ItNcG3 ? "IT-NC G3" : "ET-NC G3";
  deviceData["DEVICE_MODEL"] = ncG3.modelId < 24 ? nc_g3_models[ncG3.modelId] : "Unknown NC G3";
  deviceData["MODEL_ID"] = ncG3.modelId;
  deviceData["PV_INPUT_COUNT"] = ncG3.pvCount;
  deviceData["PV_MAX_V"] = ncG3.pvMaxVoltage / 100.f;
  deviceData["RATED_CHARGE_POWER"] = ncG3.ratedChargePower / 100.f;
  deviceData["RATED_BATTERY_V"] = ncG3.ratedBatteryVoltage / 100.f;
  deviceData["RATED_CHARGE_A"] = ncG3.ratedChargeCurrent / 100.f;
  deviceData["RATED_LOAD_A"] = ncG3.ratedLoadCurrent / 100.f;
  deviceData["DSP_FIRMWARE"] = ncG3.dspFirmware / 100.f;
  deviceData["ARM_FIRMWARE"] = ncG3.armFirmware / 100.f;
  deviceData["BATTERY_TYPE"] = ncG3.batteryType < 13 ? nc_g3_battery_types[ncG3.batteryType] : "Unknown";
  deviceData["BATTERY_CAPACITY"] = ncG3.batteryCapacity;
  deviceData["TEMPERATURE_COMPENSATION"] = ncG3.temperatureCompensation / -100.f;
  deviceData["HIGH_VOLT_DISCONNECT"] = ncG3.highVoltageDisconnect / 100.f;
  deviceData["CHARGING_LIMIT_VOLTS"] = ncG3.chargingLimitVoltage / 100.f;
  deviceData["OVER_VOLTS_RECONNECT"] = ncG3.overVoltageReconnect / 100.f;
  deviceData["EQUALIZATION_VOLTS"] = ncG3.equalizationVoltage / 100.f;
  deviceData["BOOST_VOLTS"] = ncG3.boostVoltage / 100.f;
  deviceData["FLOAT_VOLTS"] = ncG3.floatVoltage / 100.f;
  deviceData["BOOST_RECONNECT_VOLTS"] = ncG3.boostReconnectVoltage / 100.f;
  deviceData["LOW_VOLTS_RECONNECT"] = ncG3.lowVoltageReconnect / 100.f;
  deviceData["UNDER_VOLTS_RECOVER"] = ncG3.underVoltageRecover / 100.f;
  deviceData["UNDER_VOLTS_WARNING"] = ncG3.underVoltageWarning / 100.f;
  deviceData["LOW_VOLTS_DISCONNECT"] = ncG3.lowVoltageDisconnect / 100.f;
  deviceData["DISCHARGING_LIMIT_VOLTS"] = ncG3.dischargingLimitVoltage / 100.f;
  deviceData["CHARGING_CURRENT_LIMIT"] = ncG3.chargingCurrentLimit / 100.f;
  deviceData["EQUALIZATION_TIME"] = ncG3.equalizationTime;
  deviceData["BOOST_TIME"] = ncG3.boostTime;
  deviceData["LITHIUM_PROTECTION"] = ncG3.lithiumProtection == 3;
  deviceData["LOW_TEMP_CHARGE_LIMIT"] = ncG3.lowTemperatureChargeLimit / 100.f;
  deviceData["LOW_TEMP_DISCHARGE_LIMIT"] = ncG3.lowTemperatureDischargeLimit / 100.f;
  deviceData["MAX_BATTERY_TEMP"] = ncG3.maximumBatteryTemperature / 100.f;
  deviceData["MIN_BATTERY_TEMP"] = ncG3.minimumBatteryTemperature / 100.f;
  deviceData["MAX_DEVICE_TEMP"] = ncG3.maximumDeviceTemperature / 100.f;
  deviceData["DEVICE_TEMP_RECOVER"] = ncG3.deviceTemperatureRecover / 100.f;
  deviceData["CHARGING_MODE_SETTING"] = ncG3.chargingMode == 0 ? "Voltage" : "SOC";
  deviceData["FULL_SOC"] = ncG3.fullSoc;
  deviceData["FULL_SOC_RECOVER"] = ncG3.fullSocRecover;
  deviceData["DISCHARGE_RECOVER_SOC"] = ncG3.dischargeRecoverSoc;
  deviceData["LOW_POWER_RECOVER_SOC"] = ncG3.lowPowerRecoverSoc;
  deviceData["LOW_POWER_ALARM_SOC"] = ncG3.lowPowerAlarmSoc;
  deviceData["DISCHARGE_SOC"] = ncG3.dischargeSoc;
  deviceData["RECORD_PERIOD"] = ncG3.recordPeriod;
  deviceData["BMS_PROTOCOL"] = ncG3.bmsProtocol;
  deviceData["BMS_ENABLED"] = ncG3.bmsEnabled != 0;
  deviceData["PV_INPUT_MODE"] = ncG3.pvInputMode == 0 ? "Independent" : "Centralized";
  deviceData["MODBUS_ADDRESS"] = ncG3.modbusAddress;
  deviceData["BAUD_RATE_CODE"] = ncG3.baudRateCode;
  deviceData["PARALLEL_CHARGE_CURRENT_LIMIT"] = ncG3.parallelChargeCurrentLimit;

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
  bmsData["DSP_COMMUNICATION_FAULT"] = (ncG3.status[5] & 0x4000) != 0;
  if (ncG3.bmsDataValid)
  {
    bmsData["CELL_COUNT"] = ncG3.bmsCellCount;
    bmsData["PACK_V"] = ncG3.bmsPackVoltage / 100.f;
    bmsData["PACK_A"] = ncG3.bmsCurrent / 100.f;
    bmsData["FULL_CAPACITY"] = ncG3.bmsFullCapacity;
    bmsData["REMAINING_CAPACITY"] = ncG3.bmsRemainingCapacity;
    bmsData["REMAINING_MINUTES"] = ncG3.bmsRemainingMinutes;
    bmsData["MAX_CELL_TEMP"] = ncG3.bmsMaximumCellTemperature / 100.f;
    bmsData["MIN_CELL_TEMP"] = ncG3.bmsMinimumCellTemperature / 100.f;
  }
  return true;
}

bool EpeverController::updateJson(uint8_t invNum)
{
#ifdef EPEVER_SIMULATION
  (void)invNum;
  _simulation->update(_liveJson);
  return true;
#endif
  EpeverProfile profile = invNum <= MaximumDevices ? deviceProfiles[invNum] : EpeverProfile::Unknown;
  if (profile == EpeverProfile::ItNcG3 || profile == EpeverProfile::EtNcG3)
  {
    updateNcG3Json(invNum, profile);
    goto common_json;
  }

  //  for (size_t invNum = 1; invNum <= 3; invNum++) // for testing only{
  _liveJson["EP_" + String(invNum)].clear();
  _liveJson["EP_" + String(invNum)]["LiveData"]["CONNECTION"] = _errorCode;

  _liveJson["EP_" + String(invNum)]["LiveData"]["DEVICE_NUM"] = String(invNum); // for testing
  // device
  _liveJson["EP_" + String(invNum)]["LiveData"]["DEVICE_TIME"] = _unixTime.getUnix();
  _liveJson["EP_" + String(invNum)]["LiveData"]["DEVICE_TEMP"] = deviceTemperature / 100.f;
  // solar input
  _liveJson["EP_" + String(invNum)]["LiveData"]["SOLAR_V"] = live.l.pvV / 100.f;
  _liveJson["EP_" + String(invNum)]["LiveData"]["SOLAR_A"] = live.l.pvA / 100.f;
  _liveJson["EP_" + String(invNum)]["LiveData"]["SOLAR_W"] = live.l.pvW / 100.f;
  // battery
  _liveJson["EP_" + String(invNum)]["LiveData"]["BATT_SOC"] = batterySOC / 1.0f;
  _liveJson["EP_" + String(invNum)]["LiveData"]["BATT_V"] = live.l.battV / 100.f;
  _liveJson["EP_" + String(invNum)]["LiveData"]["BATT_A"] = batteryCurrent / 100.f;
  _liveJson["EP_" + String(invNum)]["LiveData"]["BATT_W"] = (int(live.l.battV / 10) * int(batteryCurrent / 10) / 100.f);
  _liveJson["EP_" + String(invNum)]["LiveData"]["BATT_STATE"] = batt_volt_status[status_batt.volt];
  _liveJson["EP_" + String(invNum)]["LiveData"]["BATT_TEMP"] = batteryTemperature / 100.f;
  _liveJson["EP_" + String(invNum)]["LiveData"]["BATT_TEMP_STATE"] = batt_temp_status[status_batt.temp];
  // load out
  _liveJson["EP_" + String(invNum)]["LiveData"]["LOAD_V"] = live.l.loadV / 100.f;
  _liveJson["EP_" + String(invNum)]["LiveData"]["LOAD_A"] = live.l.loadA / 100.f;
  _liveJson["EP_" + String(invNum)]["LiveData"]["LOAD_W"] = live.l.loadW / 100.f;
  _liveJson["EP_" + String(invNum)]["LiveData"]["LOAD_STATE"] = loadState;
  // charger
  _liveJson["EP_" + String(invNum)]["LiveData"]["CHARGER_STATE"] = charger_input_status[charger_input];
  _liveJson["EP_" + String(invNum)]["LiveData"]["CHARGER_MODE"] = charger_charging_status[charger_mode];
  // statistic
  _liveJson["EP_" + String(invNum)]["StatsData"]["SOLAR_MAX"] = stats.s.pVmax / 100.f;
  _liveJson["EP_" + String(invNum)]["StatsData"]["SOLAR_MIN"] = stats.s.pVmin / 100.f;
  _liveJson["EP_" + String(invNum)]["StatsData"]["BATT_MAX"] = stats.s.bVmax / 100.f;
  _liveJson["EP_" + String(invNum)]["StatsData"]["BATT_MIN"] = stats.s.bVmin / 100.f;
  _liveJson["EP_" + String(invNum)]["StatsData"]["CONS_DAY"] = stats.s.consEnerDay / 100.f;
  _liveJson["EP_" + String(invNum)]["StatsData"]["CONS_MON"] = stats.s.consEnerMon / 100.f;
  _liveJson["EP_" + String(invNum)]["StatsData"]["CONS_YEAR"] = stats.s.consEnerYear / 100.f;
  _liveJson["EP_" + String(invNum)]["StatsData"]["CONS_TOT"] = stats.s.consEnerTotal / 100.f;
  _liveJson["EP_" + String(invNum)]["StatsData"]["GEN_DAY"] = stats.s.genEnerDay / 100.f;
  _liveJson["EP_" + String(invNum)]["StatsData"]["GEN_MON"] = stats.s.genEnerMon / 100.f;
  _liveJson["EP_" + String(invNum)]["StatsData"]["GEN_YEAR"] = stats.s.genEnerYear / 100.f;
  _liveJson["EP_" + String(invNum)]["StatsData"]["GEN_TOT"] = stats.s.genEnerTotal / 100.f;
//  _liveJson["EP_" + String(invNum)]["StatsData"]["CO2_REDUCTION"] = stats.s.c02Reduction / 100.f;
  // device settings data
  _liveJson["EP_" + String(invNum)]["DeviceData"]["BATTERY_TYPE"] =
      settingParam.s.bTyp < (sizeof batt_type / sizeof batt_type[0]) ? batt_type[settingParam.s.bTyp] : "Unknown";
  _liveJson["EP_" + String(invNum)]["DeviceData"]["BATTERY_CAPACITY"] = settingParam.s.bCapacity /*/ 100.f*/;
  _liveJson["EP_" + String(invNum)]["DeviceData"]["TEMPERATURE_COMPENSATION"] = settingParam.s.tempCompensation / 100.f;
  _liveJson["EP_" + String(invNum)]["DeviceData"]["HIGH_VOLT_DISCONNECT"] = settingParam.s.highVDisconnect / 100.f;
  _liveJson["EP_" + String(invNum)]["DeviceData"]["CHARGING_LIMIT_VOLTS"] = settingParam.s.chLimitVolt / 100.f;
  _liveJson["EP_" + String(invNum)]["DeviceData"]["OVER_VOLTS_RECONNECT"] = settingParam.s.overVoltRecon / 100.f;
  _liveJson["EP_" + String(invNum)]["DeviceData"]["EQUALIZATION_VOLTS"] = settingParam.s.equVolt / 100.f;
  _liveJson["EP_" + String(invNum)]["DeviceData"]["BOOST_VOLTS"] = settingParam.s.boostVolt / 100.f;
  _liveJson["EP_" + String(invNum)]["DeviceData"]["FLOAT_VOLTS"] = settingParam.s.floatVolt / 100.f;
  _liveJson["EP_" + String(invNum)]["DeviceData"]["BOOST_RECONNECT_VOLTS"] = settingParam.s.boostVoltRecon / 100.f;
  _liveJson["EP_" + String(invNum)]["DeviceData"]["LOW_VOLTS_RECONNECT"] = settingParam.s.lowVoltRecon / 100.f;
  _liveJson["EP_" + String(invNum)]["DeviceData"]["UNDER_VOLTS_RECOVER"] = settingParam.s.underVoltRecov / 100.f;
  _liveJson["EP_" + String(invNum)]["DeviceData"]["UNDER_VOLTS_WARNING"] = settingParam.s.underVoltWarning / 100.f;
  _liveJson["EP_" + String(invNum)]["DeviceData"]["LOW_VOLTS_DISCONNECT"] = settingParam.s.lowVoltDiscon / 100.f;
  _liveJson["EP_" + String(invNum)]["DeviceData"]["DISCHARGING_LIMIT_VOLTS"] = settingParam.s.dischLimitVolt / 100.f;
  // }
common_json:
  _liveJson["DEVICE_QUANTITY"] = _settings.data.deviceQuantity;
  _liveJson["DEVICE_FREE_HEAP"] = ESP.getFreeHeap();
  // _liveJson["DEVICE_FREE_JSON"] = (JSON_BUFFER - _liveJson.memoryUsage());
  _liveJson["ESP_VCC"] = (ESP.getVcc() / 1000.0) + 0.3;
  _liveJson["Runtime"] = millis() / 1000;
  _liveJson["Wifi_RSSI"] = WiFi.RSSI();
  _liveJson["sw_version"] = _softwareVersion;

  for (int i = 0; i < _temperatureSensorCount; i++)
  {
    if (_temperatureSensors.getAddress(_temperatureAddress, i))
    {
      _liveJson["DS18B20_" + String(i + 1)] = _temperatureSensors.getTempC(_temperatureAddress);
    }
  }
  return true;
}
