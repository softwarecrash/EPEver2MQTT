#include "BootLoopGuard.h"

#include <user_interface.h>

#include "../Settings.h"

BootLoopGuard::BootLoopGuard(Settings &settings) : _settings(settings)
{
}

void BootLoopGuard::recordBoot()
{
  uint32_t bootCount = 0;

  // Reset reason 6 is an external reset on ESP8266. Repeated external resets
  // provide the existing physical recovery mechanism for invalid settings.
  if (ESP.getResetInfoPtr()->reason != 6)
  {
    writeCounter(0);
    return;
  }

  ESP.rtcUserMemoryRead(RtcMemoryOffset, &bootCount, sizeof(bootCount));
  if (bootCount >= FactoryResetThreshold && bootCount < CounterUpperBound)
  {
    _settings.reset();
    ESP.eraseConfig();
    ESP.reset();
    return;
  }

  writeCounter(bootCount + 1);
}

void BootLoopGuard::markBootSuccessful()
{
  writeCounter(0);
}

void BootLoopGuard::writeCounter(uint32_t value)
{
  ESP.rtcUserMemoryWrite(RtcMemoryOffset, &value, sizeof(value));
}
