#include "ApplicationRequests.h"

void ApplicationRequests::requestFactoryReset()
{
  _factoryResetRequested = true;
}

bool ApplicationRequests::consumeFactoryReset()
{
  if (!_factoryResetRequested)
    return false;
  _factoryResetRequested = false;
  return true;
}

void ApplicationRequests::requestDiscovery()
{
  _discoveryRequested = true;
}

bool ApplicationRequests::discoveryRequested() const
{
  return _discoveryRequested;
}

void ApplicationRequests::clearDiscoveryRequest()
{
  _discoveryRequested = false;
}

void ApplicationRequests::requestRestart()
{
  _restartRequestedAt = millis();
  _restartRequested = true;
}

bool ApplicationRequests::restartDue(unsigned long now,
                                     unsigned long delayMs) const
{
  return _restartRequested && now - _restartRequestedAt >= delayMs;
}
