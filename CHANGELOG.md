# Changelog

## 2.6.0-Pre0.3

- Split controller polling into common, Legacy and IT/ET-NC register readers.
- Add typed register structures and centralized register maps.
- Add IT-NC and ET-NC/G3 device profiles, telemetry and settings decoding.
- Integrate load switching, charging-current control and MPPT settings writes.
- Move Web UI runtime data and actions to JSON GET/POST APIs and WebSocket.
- Normalize decimal measurements to two decimal places for JSON, Web UI and MQTT.
- Fix browser device-time submission and preserve local controller wall-clock time.
- Split settings, polling, MQTT, temperature, requests and device commands into
  components with explicit state ownership.
- Add a register-level simulator build for Legacy, IT-NC and ET-NC devices.
- Add simulator startup self-tests and architecture invariant checks.
- Add icon-only copy-to-clipboard and text-download actions to WebSerial.
- Start the web server even when the initial Wi-Fi connection attempt fails.
- Keep the Web UI available when Wi-Fi reconnects after boot.
- Fix a WebSocket buffer overflow that could corrupt the ESP8266 heap.
- Clean up disconnected WebSocket clients regularly.
- Prevent duplicate WebSocket ping timers after browser reconnects.
- Send browser pings only while the WebSocket connection is open.
- Reserve the hardware serial port exclusively for Modbus RTU traffic.
- Route diagnostic messages to WebSerial instead of the Modbus UART.
- Add readable EPEver data output to WebSerial.
- Add the `EPEVER_WEBSERIAL_DATA_LOG` build flag to enable or disable WebSerial data logging.
- Read the controller RTC from the profile-specific Modbus registers.
- Prefer NTP system time when it is available and fall back to the controller RTC.
- Update `DEVICE_TIME` only after a successful EPEver polling cycle.
- Preserve integer JSON values while normalizing decimal measurements.
- Fix precision loss that caused Unix timestamps to appear stationary.
- Pause controller polling during OTA updates.
- Re-enable polling when an OTA update fails.
- Schedule an automatic reboot after a successful OTA update.
- Report OTA progress and errors through WebSerial.
- Initialize temperature sensors independently of the initial Wi-Fi result.
