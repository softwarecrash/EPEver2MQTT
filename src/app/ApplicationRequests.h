#pragma once

#include <Arduino.h>

class ApplicationRequests
{
public:
  void requestFactoryReset();
  bool consumeFactoryReset();

  void requestDiscovery();
  bool discoveryRequested() const;
  void clearDiscoveryRequest();

  void requestRestart();
  bool restartDue(unsigned long now, unsigned long delayMs) const;

private:
  bool _factoryResetRequested = false;
  bool _discoveryRequested = false;
  bool _restartRequested = false;
  unsigned long _restartRequestedAt = 0;
};
