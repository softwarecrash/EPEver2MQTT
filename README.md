# EPEver2MQTT
ESP82XX to MQTT based connector for EPEver Solar tracker, support multiple trackers

# Feauteres:
- set up over captive portal
- Config in webinterface
- Firmware update via webinterface
- classic MQTT Datapoints or Json String over MQTT
- get Json over web at /livejson?
- switch Load Channel via webinterface and MQTT or via web at /set?loadstate=0 or 1
- set Device time from computer time

![grafik](https://user-images.githubusercontent.com/44615614/230722020-9ee2ef7e-0f98-4094-83f2-994f6211ecad.png)
![grafik](https://user-images.githubusercontent.com/44615614/230722025-69865c5b-da78-4ed5-897f-6f1b389e878c.png)
![grafik](https://user-images.githubusercontent.com/44615614/230722029-4481645d-6a2b-47da-9472-c2f1f49fc21e.png)



#Todo:
- cleanup code
- test more devices

# [Tested with](https://github.com/softwarecrash/EPEver2MQTT/wiki/Working-Devices)


- from protocol it should work with LS-B VS-B Tracer-B Tracer-A ITracer ETracer Series
- have you sucessfull run it on another devices? please tell me your device

# How to use:
- flash the bin file to a esp82xx or Wemos D1 Mini with tasmotizer or other way
- connect the esp like the wireing diagram
- search the wifi ap EPEver2MQTT-AP and connect
- surf to 192.168.4.1 and set up your wifi, amount of inverters and optional mqtt
- thats it :)

wireing:
![Unbenannt](https://user-images.githubusercontent.com/44615614/185478302-9db8c1b2-35e8-49b4-a228-8019b8f7f845.png)



Questions? join https://discord.gg/dHeDRGdtKN
