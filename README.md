# EPEver2MQTT
ESP82XX to MQTT based connector for EPEver Solar tracker

# Feauteres:
- set up over ap
- config in webinterface
- update via webinterface
- classic MQTT Datapoints or Json String over MQTT
- get Json over web at /livejson?
- switch Load Channel via webinterface and MQTT or via web at /set?loadstate=0 or 1

![image](https://user-images.githubusercontent.com/44615614/185475929-a2917dfc-5c8c-4f71-8ca4-2964272e8856.png)
![image](https://user-images.githubusercontent.com/44615614/185475961-508b8f2f-7062-40c5-8f8d-06bc6107e580.png)
![image](https://user-images.githubusercontent.com/44615614/185476011-fbd855d5-9ca1-4da4-8310-85bdc5acc04e.png)


#Todo:
- get and set the device time from computer time, or better from ntp?
- cleanup code
- test more devices

# Tested with:
- EPEVER Tracer2210AN
- have you sucessfull run it on another devices? please tell me your device

# Known Bugs:
- Timestamp from device will zero after serveral time

# How to use:
- flash the bin file to a esp82xx or Wemos D1 Mini with tasmotizer or other way
- connect the esp like the wireing diagram
- search the wifi ap EPEver2MQTT-AP and connect
- surf to 192.168.4.1 and set up your wifi and optional mqtt
- thats it :)

wireing:
![Unbenannt](https://user-images.githubusercontent.com/44615614/185478302-9db8c1b2-35e8-49b4-a228-8019b8f7f845.png)



Questions? join https://discord.gg/dHeDRGdtKN
