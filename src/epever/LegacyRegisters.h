#pragma once

#include <Arduino.h>

namespace LegacyRegisters
{
constexpr uint16_t LiveData = 0x3100;
constexpr uint8_t LiveDataCount = 16;
constexpr uint16_t RtcClock = 0x9013;
constexpr uint8_t RtcClockCount = 3;
constexpr uint16_t BatterySoc = 0x311A;
constexpr uint16_t BatteryCurrent = 0x331B;
constexpr uint16_t BatteryTemperature = 0x3110;
constexpr uint16_t DeviceTemperature = 0x3111;
constexpr uint16_t DeviceSettings = 0x9000;
constexpr uint8_t DeviceSettingsCount = 15;
constexpr uint16_t Statistics = 0x3300;
constexpr uint8_t StatisticsCount = 20;
constexpr uint16_t LoadState = 0x0002;
constexpr uint16_t StatusFlags = 0x3200;
}
