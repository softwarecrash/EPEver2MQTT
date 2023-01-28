# EPEver2MQTT
ESP82XX to MQTT based connector for EPEver Solar tracker, support multiple trackers

# Feauteres:
- set up over captive portal
- onfig in webinterface
- Firmware update via webinterface
- classic MQTT Datapoints or Json String over MQTT
- get Json over web at /livejson?
- switch Load Channel via webinterface and MQTT or via web at /set?loadstate=0 or 1
- set Device time from computer time

![image](https://user-images.githubusercontent.com/44615614/185672807-ba14f2c1-f630-49c4-b30b-bab82101a0c7.png)
![image](https://user-images.githubusercontent.com/44615614/185672842-082d8b25-2e81-4977-91ce-fe7a9f168e4f.png)
![image](https://user-images.githubusercontent.com/44615614/185673044-8760afd0-cded-4e27-a565-e48c421af863.png)

#Todo:
- cleanup code
- test more devices

# Tested with:
- EPEVER Tracer 2210AN (Confirmed)
- EPEVER Tracer 3210AN (Confirmed)
- EPEVER XTRA 4415 (confirmed)
- EPEVER XTRA 3415N (confirmed)
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
