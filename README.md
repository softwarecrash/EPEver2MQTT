# EPEver2MQTT  [![GitHub release](https://img.shields.io/github/release/softwarecrash/EPEver2MQTT?include_prereleases=&sort=semver&color=blue)](https://github.com/softwarecrash/EPEver2MQTT/releases/latest) [![Discord](https://img.shields.io/discord/1007020337482973254?logo=discord&label=Discord)](https://discord.gg/Hup3gg4YsN)
ESP82XX to MQTT based connector for EPEver Solar tracker, support multiple trackers

# Features:
- set up over captive portal
- Config in webinterface
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

![grafik](https://user-images.githubusercontent.com/44615614/230722020-9ee2ef7e-0f98-4094-83f2-994f6211ecad.png)
![grafik](https://user-images.githubusercontent.com/44615614/230722025-69865c5b-da78-4ed5-897f-6f1b389e878c.png)
![grafik](https://user-images.githubusercontent.com/44615614/230722029-4481645d-6a2b-47da-9472-c2f1f49fc21e.png)


# How to use:
- flash your ESP8266 (recommended Wemos D1 Mini) with our [Flash2MQTT-Tool](https://all-solutions.github.io/Flash2MQTT/?get=EPEver2MQTT) or with [Tasmotizer](https://github.com/tasmota/tasmotizer/releases)
- connect the esp like the [wiring diagram](https://github.com/softwarecrash/EPEver2MQTT/wiki/Wireing)
- search the wifi ap EPEver2MQTT-AP and connect
- surf to 192.168.4.1 and set up your wifi, amount of inverters and optional mqtt
- that's it :)

# Known issues:
- Input field doesn´t like %

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
