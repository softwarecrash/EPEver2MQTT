#pragma once

#include <Arduino.h>

namespace ProjectConfig
{
constexpr uint32_t ModbusBaud = 115200;
constexpr uint8_t StatusLedPin = 2;       // D4 on a Wemos D1 mini
constexpr uint8_t TemperatureSensorPin = 4;
constexpr uint8_t MaximumDevices = 6;
constexpr uint16_t ModbusResponseTimeoutMs = 100;
constexpr uint16_t PollIntervalMs = 500;
constexpr uint16_t ErrorNotificationIntervalMs = 1000;
constexpr uint16_t RestartDelayMs = 500;
constexpr size_t WebSerialBufferSize = 256;
constexpr size_t MqttClientIdSize = 80;
constexpr size_t EepromSize = 1024;

#ifdef ARDUINO_ESP8266_ESP01
constexpr uint8_t ModbusTransceiverEnablePin = 0;
#else
constexpr uint8_t ModbusTransceiverEnablePin = 5; // D1 on a Wemos D1 mini
#endif
}
