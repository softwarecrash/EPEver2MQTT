# EPEver2MQTT  [![GitHub release](https://img.shields.io/github/release/softwarecrash/EPEver2MQTT?include_prereleases=&sort=semver&color=blue)](https://github.com/softwarecrash/EPEver2MQTT/releases/latest)
ESP82XX to MQTT based connector for EPEver Solar tracker, support multiple trackers

# Features:
- set up over captive portal
- Config in webinterface
- Firmware update via webinterface
- classic MQTT Datapoints or Json String over MQTT
- get Json over web at /livejson
- switch Load Channel via webinterface and MQTT or via web at /set?loadstate=0 or 1
- set Device time from computer time
- debug over WebSerial (no need to connect to a PC with a FTDI-Adapter)

![grafik](https://user-images.githubusercontent.com/44615614/230722020-9ee2ef7e-0f98-4094-83f2-994f6211ecad.png)
![grafik](https://user-images.githubusercontent.com/44615614/230722025-69865c5b-da78-4ed5-897f-6f1b389e878c.png)
![grafik](https://user-images.githubusercontent.com/44615614/230722029-4481645d-6a2b-47da-9472-c2f1f49fc21e.png)



# [tested Device List](https://github.com/softwarecrash/EPEver2MQTT/wiki/Working-Devices)


# How to use:
- flash the bin file to a esp82xx or Wemos D1 Mini with [Tasmotizer](https://github.com/tasmota/tasmotizer/releases) or other way
- connect the esp like the wiring diagram
- search the wifi ap EPEver2MQTT-AP and connect
- surf to 192.168.4.1 and set up your wifi, amount of inverters and optional mqtt
- that's it :)

# Wiring
![EPEVER Pinout MQTT-Projekt](https://github.com/softwarecrash/EPEver2MQTT/assets/17761850/5dd5caa6-cda8-4ed6-bee4-a13ef7d1de22)
![EPEVER Pinout MQTT-Projekt ESP-01](https://github.com/softwarecrash/EPEver2MQTT/assets/17761850/46b6339f-f861-4c12-9278-5b2244888ffb)
As voltage converter for the ESP-01 version you can use e.g. this: https://amzn.eu/d/h7PepCr

# Completely assembled and tested PCB's

You are welcome to get fully stocked and tested PCB's. These are then already loaded with the lastest firmware. The earnings from the PCBs are used for the further development of existing and new projects.

![image](https://github.com/softwarecrash/EPEver2MQTT/assets/17761850/0a9ff025-1992-49d0-b7f1-9ea1a1bc7f2a)

If interested see [here](https://all-solutions.store/?apply_coupon=NEWSHOP)

Until 15.07. there is also a discount of 5€ per PCB.


# Questions? 
[Join the Discord Channel (German / English)](https://discord.gg/5MVtGDFhKH)
#
[![LICENSE](https://licensebuttons.net/l/by-nc-sa/4.0/88x31.png)](https://creativecommons.org/licenses/by-nc-sa/4.0/)
