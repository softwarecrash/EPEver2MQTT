#pragma once

#include <Arduino.h>

namespace NcG3Registers
{
constexpr uint16_t Model = 0x3000;
constexpr uint16_t PvMaximumVoltage = 0x3002;
constexpr uint16_t ChargeRatings = 0x3004;
constexpr uint16_t RatedChargeCurrent = 0x3007;
constexpr uint16_t RatedLoadCurrent = 0x300B;
constexpr uint16_t DspFirmwareAndPvCount = 0x300E;
constexpr uint16_t ArmFirmware = 0x3011;
constexpr uint16_t Pv1Live = 0x3100;
constexpr uint16_t Pv2Live = 0x3108;
constexpr uint16_t LoadLive = 0x3110;
constexpr uint16_t BatteryVoltage = 0x3114;
constexpr uint16_t BatteryLive = 0x3117;
constexpr uint16_t PvTotals = 0x311D;
constexpr uint16_t Status = 0x3200;
constexpr uint16_t BmsStatus = 0x3205;
constexpr uint16_t BatteryStatistics = 0x3301;
constexpr uint16_t ItEnergyStatistics = 0x3303;
constexpr uint16_t EtEnergyStatistics = 0x330B;
constexpr uint16_t BmsTelemetry = 0x3400;
constexpr uint16_t BatterySettings = 0x9000;
constexpr uint8_t BatterySettingsCount = 3;
constexpr uint16_t VoltageSettings = 0x9007;
constexpr uint8_t VoltageSettingsCount = 12;
constexpr uint16_t OptionalSettingsAndRtc = 0x9014;
constexpr uint16_t TemperatureLimits = 0x901D;
constexpr uint16_t OperatingSettings = 0x9038;
constexpr uint16_t LoadState = 0x0003;
constexpr uint16_t RtcClock = 0x9019;
constexpr uint16_t ChargeCurrentLimit = 0x9013;
}
