#pragma once

#include <Arduino.h>

class Settings;

class BootLoopGuard
{
public:
  explicit BootLoopGuard(Settings &settings);

  void recordBoot();
  void markBootSuccessful();

private:
  static constexpr uint32_t RtcMemoryOffset = 16;
  static constexpr uint32_t FactoryResetThreshold = 10;
  static constexpr uint32_t CounterUpperBound = 20;

  void writeCounter(uint32_t value);

  Settings &_settings;
};
