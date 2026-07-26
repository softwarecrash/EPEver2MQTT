#pragma once

#include <Arduino.h>

enum class EpeverProfile : uint8_t
{
  Unknown,
  Legacy,
  ItNcG3,
  EtNcG3
};

inline bool isNcG3Profile(EpeverProfile profile)
{
  return profile == EpeverProfile::ItNcG3 || profile == EpeverProfile::EtNcG3;
}

inline const char *epeverProfileName(EpeverProfile profile)
{
  switch (profile)
  {
  case EpeverProfile::Legacy:
    return "Legacy";
  case EpeverProfile::ItNcG3:
    return "IT-NC G3";
  case EpeverProfile::EtNcG3:
    return "ET-NC G3";
  default:
    return "Unknown";
  }
}
