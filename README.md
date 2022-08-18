# EPEver2MQTT
ESP82XX to MQTT based connector for EPEver Solar tracker

# Feauteres:
- set up over ap
- config in webinterface
- update via webinterface
- classic MQTT Datapoints or Json String over MQTT
- get Json over web at http://xxx.xxx.xxx.xxx/livejson?
- switch Load Channel via webinterface and MQTT or via web at /set?loadstate=0 or 1

#Todo:
- get and set the device time from computer time, or better from ntp?
- cleanup code
- test more devices

# Tested with:
- EPEVER Tracer2210AN
- have you sucessfull run it on another devices? please tell me your device

# Known Bugs:
- Timestamp from device will zero after serveral time

Questions? join https://discord.gg/dHeDRGdtKN