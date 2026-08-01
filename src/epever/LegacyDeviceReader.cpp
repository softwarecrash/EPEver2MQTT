#include "EpeverController.h"

#include "../app/DiagnosticLog.h"
#include "LegacyRegisters.h"

bool EpeverController::readLegacy(uint8_t device)
{
  _errorCode = 0;
  _modbus.setSlaveId(device);
  rtc = {};
  live = {};
  stats = {};
  settingParam = {};
  status_batt = {};
  batteryCurrent = 0;
  batterySOC = 0;
  loadState = false;
  charger_input = 0;
  charger_mode = 0;
  _unixTime.setDateTime(0, 0, 0, 0, 0, 0);

  if (!readHoldingBlock(LegacyRegisters::RtcClock,
                        LegacyRegisters::RtcClockCount, rtc.words))
  {
    _errorCode = result;
    DiagnosticLog::printf(
        "[EPEVER:%u] RTC register read failed (code %u)", device, result);
    return false;
  }
  _unixTime.setDateTime(2000 + rtc.value.year, rtc.value.month,
                        rtc.value.day, rtc.value.hour, rtc.value.minute,
                        rtc.value.second);

  if (!readInputBlock(LegacyRegisters::LiveData,
                      LegacyRegisters::LiveDataCount, live.words))
  {
    _errorCode = result;
    DiagnosticLog::printf(
        "[EPEVER:%u] Live register read failed (code %u)", device, result);
    return false;
  }

  if (!readInputBlock(LegacyRegisters::Statistics,
                      LegacyRegisters::StatisticsCount, stats.words))
  {
    _errorCode = result;
    DiagnosticLog::printf(
        "[EPEVER:%u] Statistics read failed (code %u)", device, result);
    return false;
  }

  uint16_t values[2] = {};
  if (!readInputBlock(LegacyRegisters::BatterySoc, 1, values))
  {
    _errorCode = result;
    DiagnosticLog::printf(
        "[EPEVER:%u] Battery SOC read failed (code %u)", device, result);
    return false;
  }
  batterySOC = values[0];

  if (!readInputBlock(LegacyRegisters::BatteryCurrent, 2, values))
  {
    _errorCode = result;
    DiagnosticLog::printf(
        "[EPEVER:%u] Battery current read failed (code %u)", device, result);
    return false;
  }
  batteryCurrent =
      static_cast<int32_t>(wordsToUint32(values[0], values[1]));

  if (!readCoil(LegacyRegisters::LoadState, loadState))
  {
    _errorCode = result;
    DiagnosticLog::printf(
        "[EPEVER:%u] Load state read failed (code %u)", device, result);
    return false;
  }
  if (!readInputBlock(LegacyRegisters::StatusFlags, 2, values))
  {
    _errorCode = result;
    DiagnosticLog::printf(
        "[EPEVER:%u] Status register read failed (code %u)", device, result);
    return false;
  }
  status_batt.voltage = values[0] & 0x0F;
  status_batt.temperature = (values[0] >> 4) & 0x0F;
  status_batt.resistanceAbnormal = (values[0] & 0x0100) != 0;
  status_batt.ratedVoltageInvalid = (values[0] & 0x8000) != 0;
  charger_mode = (values[1] >> 2) & 0x03;

  if (!readInputBlock(LegacyRegisters::DeviceTemperature, 1, values))
  {
    _errorCode = result;
    DiagnosticLog::printf(
        "[EPEVER:%u] Device temperature read failed (code %u)",
        device, result);
    return false;
  }
  deviceTemperature = static_cast<int16_t>(values[0]);

  if (!readInputBlock(LegacyRegisters::BatteryTemperature, 1, values))
  {
    _errorCode = result;
    DiagnosticLog::printf(
        "[EPEVER:%u] Battery temperature read failed (code %u)",
        device, result);
    return false;
  }
  batteryTemperature = static_cast<int16_t>(values[0]);

  if (!readHoldingBlock(LegacyRegisters::DeviceSettings,
                        LegacyRegisters::DeviceSettingsCount,
                        settingParam.words))
  {
    _errorCode = result;
    DiagnosticLog::printf(
        "[EPEVER:%u] Settings read failed (code %u)", device, result);
    return false;
  }

  synchronizeDeviceClock(device);
  DiagnosticLog::printf("[EPEVER:%u] Legacy poll successful", device);
  return true;
}
