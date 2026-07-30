# EPEver2MQTT  [![GitHub release](https://img.shields.io/github/release/softwarecrash/EPEver2MQTT?include_prereleases=&sort=semver&color=blue)](https://github.com/softwarecrash/EPEver2MQTT/releases/latest) [![Discord](https://img.shields.io/discord/1007020337482973254?logo=discord&label=Discord)](https://discord.gg/Hup3gg4YsN)
ESP82XX to MQTT based connector for EPEver Solar tracker, support multiple trackers


# Features:
- set up over captive portal
- Config in webinterface
- Read, write, verify, and clone MPPT battery settings from the web interface
- [Multiple Device Support](https://github.com/softwarecrash/EPEver2MQTT/wiki/Multi-Device-Tutorial)
- Firmware update via webinterface
- MQTT Datapoints or Json String over MQTT
- get Json over web at /livejson
- switch Load Channel via webinterface and MQTT
- set Device time from computer time
- debug over WebSerial (no need to connect to a PC with a FTDI-Adapter)
- with Teapod
- [Home Assistant Integration](https://github.com/softwarecrash/EPEver2MQTT/wiki/HomeAssistant-Integration)
- [External Temperatur Sensors](https://github.com/softwarecrash/EPEver2MQTT/wiki/Wiring-temperature-sensors)
- dedicated simulator firmware for testing without an EPEVER controller

![grafik](https://user-images.githubusercontent.com/44615614/230722020-9ee2ef7e-0f98-4094-83f2-994f6211ecad.png)
![grafik](https://user-images.githubusercontent.com/44615614/230722025-69865c5b-da78-4ed5-897f-6f1b389e878c.png)
![grafik](https://user-images.githubusercontent.com/44615614/230722029-4481645d-6a2b-47da-9472-c2f1f49fc21e.png)


# How to use:
- flash your ESP8266 (recommended Wemos D1 Mini) with our [Flash2MQTT-Tool](https://flash.2mqtt.de/?get=Epever2MQTT) or our desktop [2MQTT-Flasher](https://github.com/all-solutions/2MQTT-Flasher)
- connect the esp like the [wiring diagram](https://github.com/softwarecrash/EPEver2MQTT/wiki/Wireing)
- search the wifi ap EPEver2MQTT-AP and connect
- surf to 192.168.4.1 and set up your wifi, amount of inverters and optional mqtt
- that's it :)

# Known issues:
- Input field doesn´t like %

# Simulator build

The `simulator` PlatformIO environment replaces Modbus communication with an
internal, stateful data source. It is compiled only for this environment; the
normal `d1_mini` and `esp01_1m` firmware do not contain simulator code.

```sh
pio run -e simulator
```

Flash the resulting firmware to a Wemos D1 Mini and configure Wi-Fi in the
usual captive portal. No EPEVER controller or RS485 adapter is required. The
simulation exposes three controllers:

- a 12 V legacy Tracer controller
- a 24 V IT6415NC G3 with load output and BMS values
- a 48 V ET6415NC G3 with two PV inputs

One simulated day takes three minutes. Solar production, clouds, temperatures,
battery state, load consumption and energy counters vary continuously. Web UI
and MQTT control commands change load state, charging-current limit, battery
settings and device time in the simulated data source.

# Web API

The Web UI is a static client of the JSON API and WebSocket. HTTP Basic
authentication, when configured, applies to both pages and API endpoints.

| Method | Endpoint | Purpose |
| --- | --- | --- |
| `GET` | `/api/system` | Device/UI metadata and simulator flag |
| `GET` | `/api/settings` | Current application settings |
| `POST` | `/api/settings` | Validate and store JSON settings |
| `GET` | `/livejson` | Complete current controller data |
| `POST` | `/api/device-time` | Set all controller clocks |
| `GET` | `/api/mppt/settings?device=1` | Read battery settings |
| `POST` | `/api/mppt/settings` | Write and verify battery settings |
| `POST` | `/api/mppt/clone` | Clone settings to compatible devices |
| `POST` | `/api/device-address` | Change a controller Modbus address |
| `POST` | `/api/ha-discovery` | Schedule Home Assistant discovery |
| `POST` | `/api/reboot` | Schedule restart |
| `POST` | `/api/reset` | Schedule factory reset |
| `POST` | `/update` | Firmware upload (multipart, not JSON) |

Live data is sent as JSON through `/ws`. Client commands use JSON as well:

```json
{"type":"setLoad","device":1,"state":true}
```

```json
{"type":"ping"}
```

# Source layout

- `src/app`: polling, boot recovery and status indication
- `src/epever`: controller profiles, Modbus reads/writes and battery/clock services
- `src/mqtt`: MQTT transport and Home Assistant discovery
- `src/network`: Wi-Fi and captive-portal setup
- `src/web`: HTTP JSON routes and WebSocket handling
- `src/simulation`: simulator-only data source
- `src/webpages`: static Web UI source compiled into `src/html.h`

### How-To video by Jarnsen

<a href="http://www.youtube.com/watch?feature=player_embedded&v=Gx0PdaDmH3k" target="_blank">
 <img src="http://img.youtube.com/vi/Gx0PdaDmH3k/0.jpg" alt="Watch the video" />
</a>

# Completely assembled and tested PCB's

You are welcome to get fully stocked and tested PCB's. These are then already loaded with the lastest firmware. The earnings from the PCBs are used for the further development of existing and new projects.

[![image](https://github.com/softwarecrash/EPEver2MQTT/assets/17761850/0a9ff025-1992-49d0-b7f1-9ea1a1bc7f2a)](https://all-solutions.store)

If interested see [here](https://all-solutions.store)

#
[<img src="https://cdn.buymeacoffee.com/buttons/default-orange.png" alt="Buy Me A Coffee" height="41" width="174"/>](https://donate.softwarecrash.de)
#
[![LICENSE](https://licensebuttons.net/l/by-nc-nd/4.0/88x31.png)](https://creativecommons.org/licenses/by-nc-nd/4.0/)
