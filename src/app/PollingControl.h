#pragma once

#include <Arduino.h>

enum class PollingPauseReason : uint8_t
{
  DeviceCommand = 1U << 0,
  OtaUpdate = 1U << 1
};

class PollingControl
{
public:
  bool canRun() const
  {
    return _deviceCommandDepth == 0 && !_otaPaused;
  }

  void pause(PollingPauseReason reason)
  {
    if (reason == PollingPauseReason::DeviceCommand)
    {
      if (_deviceCommandDepth < UINT8_MAX)
        _deviceCommandDepth++;
      return;
    }
    _otaPaused = true;
  }

  void resume(PollingPauseReason reason)
  {
    if (reason == PollingPauseReason::DeviceCommand)
    {
      if (_deviceCommandDepth > 0)
        _deviceCommandDepth--;
      return;
    }
    _otaPaused = false;
  }

private:
  // Device commands may be nested. A counter prevents an inner command from
  // accidentally resuming polling while an outer command still owns the bus.
  uint8_t _deviceCommandDepth = 0;
  bool _otaPaused = false;
};

class ScopedPollingPause
{
public:
  explicit ScopedPollingPause(PollingControl &control)
      : _control(control)
  {
    _control.pause(PollingPauseReason::DeviceCommand);
  }

  ~ScopedPollingPause()
  {
    _control.resume(PollingPauseReason::DeviceCommand);
  }

private:
  PollingControl &_control;
};
