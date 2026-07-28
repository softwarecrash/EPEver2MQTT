#include "SimulationDataSource.h"

#include <UnixTime.h>
#include <math.h>

#include "../epever/LegacyRegisters.h"
#include "../epever/NcG3Registers.h"

namespace
{
constexpr float SimulatedDayMilliseconds = 180000.0f;
constexpr uint32_t BaseUnixTime = 1785062400UL;

uint16_t toUnsignedRaw(float value)
{
  if (value <= 0)
    return 0;
  if (value >= 655.35f)
    return UINT16_MAX;
  return static_cast<uint16_t>(roundf(value * 100.0f));
}

int16_t toSignedRaw(float value)
{
  return static_cast<int16_t>(roundf(value * 100.0f));
}
}

SimulationDataSource::SimulationDataSource()
    : _loadState{false, true, true, false},
      _chargeCurrentLimit{0, 30, 60, 60},
      _generatedToday{},
      _consumedToday{},
      _batterySettings{},
      _lastUpdate{},
      _clockOffset(0)
{
  const uint16_t defaults[BatterySettingCount] = {
      0, 200, 300, 1460, 1450, 1380, 1450, 1380,
      1320, 1260, 1260, 1220, 1200, 1160, 1100};
  for (uint8_t device = 1; device <= DeviceCount; device++)
  {
    memcpy(_batterySettings[device], defaults, sizeof(defaults));
    const uint8_t voltageMultiplier =
        device == 1 ? 1 : (device == 2 ? 2 : 4);
    for (uint8_t index = 3; index < BatterySettingCount; index++)
      _batterySettings[device][index] *= voltageMultiplier;
  }
  _batterySettings[2][0] = 5;
  _batterySettings[3][0] = 7;
}

void SimulationDataSource::begin()
{
  const unsigned long now = millis();
  for (uint8_t device = 1; device <= DeviceCount; device++)
    _lastUpdate[device] = now;
}

bool SimulationDataSource::selfTest()
{
  uint16_t values[20] = {};

  if (!prepareDevice(1) ||
      !readInputRegisters(1, NcG3Registers::Model, 1, values) ||
      values[0] != 100 ||
      !readInputRegisters(1, LegacyRegisters::LiveData, 16, values) ||
      !readInputRegisters(1, LegacyRegisters::Statistics, 20, values) ||
      !readHoldingRegisters(1, LegacyRegisters::RtcClock, 3, values) ||
      !readCoils(1, LegacyRegisters::LoadState, 1, values))
    return false;

  if (!prepareDevice(2) ||
      !readInputRegisters(2, NcG3Registers::Model, 1, values) ||
      values[0] != 5 ||
      !readInputRegisters(2, NcG3Registers::Pv1Live, 4, values) ||
      !readInputRegisters(2, NcG3Registers::ItEnergyStatistics, 16,
                          values) ||
      !readInputRegisters(2, NcG3Registers::BmsTelemetry, 10, values) ||
      !readHoldingRegisters(2, NcG3Registers::VoltageSettings, 13,
                            values))
    return false;

  if (!prepareDevice(3) ||
      !readInputRegisters(3, NcG3Registers::Model, 1, values) ||
      values[0] != 17 ||
      !readInputRegisters(3, NcG3Registers::Pv2Live, 4, values) ||
      !readInputRegisters(3, NcG3Registers::EtEnergyStatistics, 8,
                          values) ||
      !readHoldingRegisters(
          3, NcG3Registers::OptionalSettingsAndRtc, 9, values))
    return false;

  // Invalid addresses and device IDs must fail instead of returning
  // plausible zero-filled data that could hide decoder errors.
  return !prepareDevice(0) &&
         !readInputRegisters(3, 0xFFFF, 1, values);
}

bool SimulationDataSource::prepareDevice(uint8_t device)
{
  if (device == 0 || device > DeviceCount)
    return false;

  const unsigned long now = millis();
  const float elapsedHours =
      (now - _lastUpdate[device]) / SimulatedDayMilliseconds * 24.0f;
  _lastUpdate[device] = now;

  const float dayProgress =
      fmodf(now, SimulatedDayMilliseconds) / SimulatedDayMilliseconds;
  const float hours = dayProgress * 24.0f;
  const float sunAngle = (hours - 6.0f) / 12.0f * PI;
  const float clearSky = hours > 6.0f && hours < 18.0f
                             ? fmaxf(0.0f, sinf(sunAngle))
                             : 0.0f;
  const float cloud =
      0.82f + 0.12f * sinf(now / 13700.0f) +
      0.06f * sinf(now / 4100.0f);
  const float solarFactor = clamp(clearSky * cloud, 0.0f, 1.0f);

  const float ratedPower[] = {0, 520.0f, 1040.0f, 1040.0f};
  const float batteryNominal[] = {0, 12.8f, 25.6f, 48.0f};
  const float loadBase[] = {0, 36.0f, 92.0f, 140.0f};
  const float pvRatedVoltage[] = {0, 38.0f, 76.0f, 112.0f};

  const float pvPower =
      ratedPower[device] * solarFactor *
      (0.96f + 0.03f * sinf(device + now / 6000.0f));
  const float loadPower =
      _loadState[device]
          ? loadBase[device] *
                (1.0f + 0.18f * sinf(hours * 0.8f + device))
          : 0.0f;
  const float batteryPower = pvPower - loadPower - pvPower * 0.035f;
  const float socWave =
      58.0f + 28.0f * sinf((hours - 10.0f) / 24.0f * 2 * PI);
  const float soc = clamp(socWave - device * 3.0f, 18.0f, 98.0f);
  const float batteryVoltage =
      batteryNominal[device] *
      (0.91f + 0.0017f * soc +
       (batteryPower > 0 ? 0.018f : -0.012f));
  const float batteryCurrent = batteryPower / batteryVoltage;
  const float pvVoltage =
      solarFactor > 0.01f ? pvRatedVoltage[device] : 0.0f;
  const float pvCurrent = pvVoltage > 0 ? pvPower / pvVoltage : 0.0f;

  _generatedToday[device] +=
      fmaxf(0.0f, pvPower) * elapsedHours / 1000.0f;
  _consumedToday[device] += loadPower * elapsedHours / 1000.0f;

  _snapshot = {};
  _snapshot.pvVoltage = toUnsignedRaw(pvVoltage);
  _snapshot.pvCurrent = toUnsignedRaw(pvCurrent);
  _snapshot.pvPower =
      static_cast<uint32_t>(roundf(fmaxf(0.0f, pvPower) * 100.0f));
  if (device == 3)
  {
    const float pv2Power = pvPower * 0.47f;
    const float pv2Voltage = pvVoltage * 0.98f;
    _snapshot.pv2Voltage = toUnsignedRaw(pv2Voltage);
    _snapshot.pv2Current =
        toUnsignedRaw(pv2Voltage > 0 ? pv2Power / pv2Voltage : 0);
    _snapshot.pv2Power =
        static_cast<uint32_t>(roundf(pv2Power * 100.0f));
  }
  _snapshot.loadVoltage = toUnsignedRaw(batteryVoltage);
  _snapshot.loadCurrent =
      toUnsignedRaw(batteryVoltage > 0 ? loadPower / batteryVoltage : 0);
  _snapshot.loadPower =
      static_cast<uint32_t>(roundf(loadPower * 100.0f));
  _snapshot.batteryVoltage = toUnsignedRaw(batteryVoltage);
  _snapshot.batteryCurrent = toSignedRaw(batteryCurrent);
  _snapshot.batterySoc = static_cast<uint16_t>(roundf(soc));
  _snapshot.batteryTemperature =
      toSignedRaw(23.5f + solarFactor * 5.0f);
  _snapshot.deviceTemperature =
      toSignedRaw(27.0f + solarFactor * 18.0f + device);
  _snapshot.systemVoltage = toUnsignedRaw(batteryNominal[device]);
  _snapshot.highestPvVoltage =
      toUnsignedRaw(pvRatedVoltage[device] * 1.08f);
  _snapshot.totalPvCurrent = _snapshot.pvCurrent;
  _snapshot.totalPvPower = _snapshot.pvPower;
  _snapshot.batteryMaximum =
      toUnsignedRaw(batteryNominal[device] * 1.14f);
  _snapshot.batteryMinimum =
      toUnsignedRaw(batteryNominal[device] * 0.91f);
  _snapshot.consumedDay =
      static_cast<uint32_t>(roundf(_consumedToday[device] * 100.0f));
  _snapshot.generatedDay =
      static_cast<uint32_t>(roundf(_generatedToday[device] * 100.0f));
  _snapshot.daytime = solarFactor > 0.0f;
  _preparedDevice = device;
  return true;
}

bool SimulationDataSource::readInputRegisters(
    uint8_t device, uint16_t address, uint8_t count,
    uint16_t *values) const
{
  if (device != _preparedDevice || values == nullptr)
    return false;
  memset(values, 0, count * sizeof(uint16_t));

  const EpeverProfile deviceProfile = profile(device);
  if (address == NcG3Registers::Model && count == 1)
  {
    values[0] = device == 1 ? 100 : (device == 2 ? 5 : 17);
    return true;
  }

  if (deviceProfile == EpeverProfile::Legacy)
  {
    if (address == LegacyRegisters::LiveData && count == 16)
    {
      values[0] = _snapshot.pvVoltage;
      values[1] = _snapshot.pvCurrent;
      setUint32(values, 2, _snapshot.pvPower);
      values[4] = _snapshot.batteryVoltage;
      values[5] = static_cast<uint16_t>(_snapshot.batteryCurrent);
      setUint32(values, 6,
                static_cast<uint32_t>(
                    static_cast<int32_t>(_snapshot.batteryVoltage) *
                    _snapshot.batteryCurrent / 100));
      values[12] = _snapshot.loadVoltage;
      values[13] = _snapshot.loadCurrent;
      setUint32(values, 14, _snapshot.loadPower);
      return true;
    }
    if (address == LegacyRegisters::Statistics && count == 20)
    {
      values[0] = _snapshot.highestPvVoltage;
      values[2] = _snapshot.batteryMaximum;
      values[3] = _snapshot.batteryMinimum;
      setUint32(values, 4, _snapshot.consumedDay);
      setUint32(values, 12, _snapshot.generatedDay);
      setUint32(values, 14, 9180 + _snapshot.generatedDay);
      setUint32(values, 16, 108740 + _snapshot.generatedDay);
      setUint32(values, 18, 492630 + _snapshot.generatedDay);
      return true;
    }
    if (address == LegacyRegisters::BatterySoc && count == 1)
      values[0] = _snapshot.batterySoc;
    else if (address == LegacyRegisters::BatteryCurrent && count == 2)
      setUint32(values, 0,
                static_cast<uint32_t>(
                    static_cast<int32_t>(_snapshot.batteryCurrent)));
    else if (address == LegacyRegisters::StatusFlags && count == 2)
      values[1] = _snapshot.daytime ? (2U << 2) : 0;
    else if (address == LegacyRegisters::DeviceTemperature && count == 1)
      values[0] = static_cast<uint16_t>(_snapshot.deviceTemperature);
    else if (address == LegacyRegisters::BatteryTemperature && count == 1)
      values[0] = static_cast<uint16_t>(_snapshot.batteryTemperature);
    else
      return false;
    return true;
  }

  if (address == NcG3Registers::PvMaximumVoltage && count == 1)
    values[0] = toUnsignedRaw(device == 2 ? 100.0f : 150.0f);
  else if (address == NcG3Registers::ChargeRatings && count == 4)
  {
    setUint32(values, 0, 104000);
    values[2] = _snapshot.systemVoltage;
    values[3] = 6000;
  }
  else if (address == NcG3Registers::RatedLoadCurrent && count == 1)
    values[0] = 3000;
  else if (address == NcG3Registers::DspFirmwareAndPvCount && count == 2)
  {
    values[0] = 102;
    values[1] = device == 3 ? 2 : 1;
  }
  else if (address == NcG3Registers::ArmFirmware && count == 1)
    values[0] = 105;
  else if (address == NcG3Registers::Pv1Live && count == 4)
  {
    values[0] = _snapshot.pvVoltage;
    values[1] = _snapshot.pvCurrent;
    setUint32(values, 2, _snapshot.pvPower);
  }
  else if (address == NcG3Registers::Pv2Live && count == 4)
  {
    values[0] = _snapshot.pv2Voltage;
    values[1] = _snapshot.pv2Current;
    setUint32(values, 2, _snapshot.pv2Power);
  }
  else if (address == NcG3Registers::LoadLive && count == 4)
  {
    values[0] = _snapshot.loadVoltage;
    values[1] = _snapshot.loadCurrent;
    setUint32(values, 2, _snapshot.loadPower);
  }
  else if (address == NcG3Registers::BatteryVoltage && count == 1)
    values[0] = _snapshot.batteryVoltage;
  else if (address == NcG3Registers::BatteryLive && count == 4)
  {
    values[0] = static_cast<uint16_t>(_snapshot.batteryCurrent);
    values[1] = static_cast<uint16_t>(_snapshot.batteryTemperature);
    values[2] = _snapshot.batterySoc;
    values[3] = static_cast<uint16_t>(_snapshot.deviceTemperature);
  }
  else if (address == NcG3Registers::PvTotals && count == 5)
  {
    values[0] = _snapshot.systemVoltage;
    values[1] = _snapshot.highestPvVoltage;
    values[2] = _snapshot.totalPvCurrent;
    setUint32(values, 3, _snapshot.totalPvPower);
  }
  else if (address == NcG3Registers::Status && count == 4)
  {
    values[2] = (_snapshot.daytime ? 0x0002 : 0) |
                (_snapshot.daytime ? (2U << 2) : 0);
    values[3] = _snapshot.daytime ? 0x0020 : 0;
  }
  else if (address == NcG3Registers::BmsStatus && count == 1)
    values[0] = device == 2 ? 0x0001 : 0;
  else if (address == NcG3Registers::BatteryStatistics && count == 2)
  {
    values[0] = _snapshot.batteryMaximum;
    values[1] = _snapshot.batteryMinimum;
  }
  else if (address == NcG3Registers::ItEnergyStatistics && count == 16)
  {
    setUint32(values, 0, _snapshot.consumedDay);
    setUint32(values, 2, 6240 + _snapshot.consumedDay);
    setUint32(values, 4, 73420 + _snapshot.consumedDay);
    setUint32(values, 6, 284170 + _snapshot.consumedDay);
    setUint32(values, 8, _snapshot.generatedDay);
    setUint32(values, 10, 9180 + _snapshot.generatedDay);
    setUint32(values, 12, 108740 + _snapshot.generatedDay);
    setUint32(values, 14, 492630 + _snapshot.generatedDay);
  }
  else if (address == NcG3Registers::EtEnergyStatistics && count == 8)
  {
    setUint32(values, 0, _snapshot.generatedDay);
    setUint32(values, 2, 9180 + _snapshot.generatedDay);
    setUint32(values, 4, 108740 + _snapshot.generatedDay);
    setUint32(values, 6, 492630 + _snapshot.generatedDay);
  }
  else if (address == NcG3Registers::BmsTelemetry && count == 10 &&
           device == 2)
  {
    values[0] = 8;
    values[1] = _snapshot.batteryVoltage;
    values[2] = static_cast<uint16_t>(_snapshot.batteryCurrent);
    values[5] = 280;
    values[6] = static_cast<uint16_t>(
        280UL * _snapshot.batterySoc / 100UL);
    values[8] = toUnsignedRaw(28.0f);
    values[9] = toUnsignedRaw(26.0f);
  }
  else
    return false;
  return true;
}

bool SimulationDataSource::readHoldingRegisters(
    uint8_t device, uint16_t address, uint8_t count,
    uint16_t *values) const
{
  if (device != _preparedDevice || values == nullptr)
    return false;
  memset(values, 0, count * sizeof(uint16_t));

  if (profile(device) == EpeverProfile::Legacy)
  {
    if (address == LegacyRegisters::DeviceSettings && count == 15)
    {
      memcpy(values, _batterySettings[device],
             BatterySettingCount * sizeof(uint16_t));
      return true;
    }
    if (address != LegacyRegisters::RtcClock || count != 3)
      return false;
  }
  else
  {
    if (address == NcG3Registers::BatterySettings && count == 3)
    {
      memcpy(values, _batterySettings[device], 3 * sizeof(uint16_t));
      return true;
    }
    if (address == NcG3Registers::VoltageSettings && count == 13)
    {
      memcpy(values, _batterySettings[device] + 3,
             12 * sizeof(uint16_t));
      values[12] = toUnsignedRaw(_chargeCurrentLimit[device]);
      return true;
    }
    if (address == NcG3Registers::OptionalSettingsAndRtc && count == 9)
    {
      values[0] = 120;
      values[1] = 120;
      values[2] = 3;
      UnixTime dateTime(0);
      dateTime.getDateTime(simulatedTime());
      values[5] = (static_cast<uint16_t>(dateTime.minute) << 8) |
                  dateTime.second;
      values[6] = (static_cast<uint16_t>(dateTime.day) << 8) |
                  dateTime.hour;
      values[7] =
          (static_cast<uint16_t>(dateTime.year - 2000) << 8) |
          dateTime.month;
      values[8] = toUnsignedRaw(60.0f);
      return true;
    }
    if (address == NcG3Registers::TemperatureLimits && count == 3)
    {
      values[0] = static_cast<uint16_t>(toSignedRaw(-20.0f));
      values[1] = toUnsignedRaw(85.0f);
      values[2] = toUnsignedRaw(75.0f);
      return true;
    }
    if (address == NcG3Registers::OperatingSettings && count == 16)
    {
      values[1] = 100;
      values[2] = 95;
      values[13] = device;
      values[14] = 4;
      values[15] = 6000;
      return true;
    }
    return false;
  }

  UnixTime dateTime(0);
  dateTime.getDateTime(simulatedTime());
  values[0] = (static_cast<uint16_t>(dateTime.minute) << 8) |
              dateTime.second;
  values[1] = (static_cast<uint16_t>(dateTime.day) << 8) |
              dateTime.hour;
  values[2] =
      (static_cast<uint16_t>(dateTime.year - 2000) << 8) |
      dateTime.month;
  return true;
}

bool SimulationDataSource::readCoils(uint8_t device, uint16_t address,
                                     uint8_t count,
                                     uint16_t *values) const
{
  if (device != _preparedDevice || values == nullptr || count != 1)
    return false;
  const uint16_t expectedAddress =
      profile(device) == EpeverProfile::Legacy
          ? LegacyRegisters::LoadState
          : NcG3Registers::LoadState;
  if (address != expectedAddress || device == 3)
    return false;
  values[0] = _loadState[device] ? 1 : 0;
  return true;
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
    if (!isDigit(dateTime[index * 2]) ||
        !isDigit(dateTime[index * 2 + 1]))
      return false;
    parts[index] = (dateTime[index * 2] - '0') * 10 +
                   dateTime[index * 2 + 1] - '0';
  }
  UnixTime targetTime(0);
  targetTime.setDateTime(2000 + parts[0], parts[1], parts[2],
                         parts[3], parts[4], parts[5]);
  _clockOffset = static_cast<int32_t>(
      static_cast<int64_t>(targetTime.getUnix()) -
      static_cast<int64_t>(BaseUnixTime) -
      static_cast<int64_t>(
          millis() / SimulatedDayMilliseconds * 86400.0f));
  return true;
}

void SimulationDataSource::setUint32(uint16_t *values, uint8_t index,
                                     uint32_t value)
{
  values[index] = static_cast<uint16_t>(value);
  values[index + 1] = static_cast<uint16_t>(value >> 16);
}

uint32_t SimulationDataSource::simulatedTime() const
{
  return BaseUnixTime + _clockOffset +
         static_cast<uint32_t>(
             millis() / SimulatedDayMilliseconds * 86400.0f);
}

float SimulationDataSource::clamp(float value, float minimum,
                                  float maximum)
{
  return fmaxf(minimum, fminf(maximum, value));
}
