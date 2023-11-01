#define ARDUINOJSON_USE_DOUBLE 0
#define ARDUINOJSON_USE_LONG_LONG 0

#define EPEVER_BAUD 115200   // baud rate for modbus
#define EPEVER_DE_RE 5       // connect DE and Re to pin D1
#define LED_PIN 02 //D4 with the LED on Wemos D1 Mini

#define ESP01
#ifdef ARDUINO_ESP8266_ESP01
#undef EPEVER_DE_RE
#define EPEVER_DE_RE 0  // ESP01 
#ifdef ESP01
#undef ESP01
#define ESP01 "display:none;"
#endif 
#endif

#define JSON_BUFFER 8192
#define MQTT_BUFFER 512
#define MAX_DEVICES 6
#define EPEVER_SERIAL Serial

// DON'T edit version here, place version number in platformio.ini (custom_prog_version) !!!
#define SOFTWARE_VERSION SWVERSION
#define FlashSize ESP.getFreeSketchSpace()

#define DEBUG_WEB(...) WebSerial.print(__VA_ARGS__)
#define DEBUG_WEBLN(...) WebSerial.println(__VA_ARGS__)
#define DEBUG_WEBF(...) WebSerial.printf(__VA_ARGS__)

bool getEpData(int invNum);

bool getJsonData(int invNum);

void callback(char *top, byte *payload, unsigned int length);

bool sendtoMQTT();

bool epWorker();

bool  sendHaDiscovery();
static const char *const haDescriptor[][4]{
    //LiveData
  {"CONNECTION","mdi:connection","",""},
  {"DEVICE_NUM","mdi:numeric","",""},
  {"DEVICE_TIME","mdi:timer-outline","",""},
  {"DEVICE_TEMP","mdi:thermometer-lines","°C","temperature"},
  {"SOLAR_V","mdi:solar-power","V","voltage"},
  {"SOLAR_A","mdi:solar-power","A","current"},
  {"SOLAR_W","mdi:solar-power","W","power"},
  {"BATT_SOC","car-battery","%","battery"},
  {"BATT_V","mdi:battery","V","voltage"},
  {"BATT_A","mdi:battery","A","current"},
  {"BATT_W","mdi:battery","W","power"},
  {"BATT_STATE","mdi:state-machine","",""},
  {"BATT_TEMP","mdi:thermometer-lines","°C","temperature"},
  {"BATT_TEMP_STATE","","",""},
  {"LOAD_V","mdi:battery-charging-medium","V","voltage"},
  {"LOAD_A","mdi:battery-charging-medium","A","current"},
  {"LOAD_W","mdi:battery-charging-medium","W","power"},
  {"LOAD_STATE","mdi:power-socket-us","",""},
  {"CHARGER_STATE","mdi:ev-station","",""},
  {"CHARGER_MODE","","",""},
  // StatsData
  {"SOLAR_MAX","mdi:sun-angle","W","power"},
  {"SOLAR_MIN","mdi:sun-angle-outline","W","power"},
  {"BATT_MAX","mdi:battery-arrow-up-outline","W","power"},
  {"BATT_MIN","mdi:battery-arrow-down-outline","W","power"},
  {"CONS_DAY","mdi:solar-power-variant","Wh","energy"},
  {"CONS_MON","mdi:solar-power-variant","Wh","energy"},
  {"CONS_YEAR","mdi:solar-power-variant","Wh","energy"},
  {"CONS_TOT","mdi:solar-power-variant","Wh","energy"},
  {"GEN_DAY","mdi:solar-power","Wh","energy"},
  {"GEN_MON","mdi:solar-power","Wh","energy"},
  {"GEN_YEAR","mdi:solar-power","Wh","energy"},
  {"GEN_TOT","mdi:solar-power","Wh","energ"},
  {"CO2_REDUCTION","","",""},
  // DeviceData
  {"BATTERY_TYPE","mdi:fuel-cell","",""},
  {"BATTERY_CAPACITY","mdi:car-battery","Wh","energy_storage"},
  {"TEMPERATURE_COMPENSATION","","mV","voltage"},
  {"HIGH_VOLT_DISCONNECT","mdi:flash-triangle-outline","V","voltage"},
  {"CHARGING_LIMIT_VOLTS","mdi:battery-minus-outline","V","voltage"},
  {"OVER_VOLTS_RECONNECT","mdi:battery-sync-outline","V","voltage"},
  {"EQUALIZATION_VOLTS","mdi:battery-sync-outline","V","voltage"},
  {"BOOST_VOLTS","mdi:battery-plus","V","voltage"},
  {"FLOAT_VOLTS","mdi:battery-plus-variant","V","voltage"},
  {"BOOST_RECONNECT_VOLTS","mdi:battery-plus","V","voltage"},
  {"LOW_VOLTS_RECONNECT","mdi:flash-triangle-outline","V","voltage"},
  {"UNDER_VOLTS_RECOVER","mdi:flash-triangle-outline","V","voltage"},
  {"UNDER_VOLTS_WARNING","mdi:flash-triangle-outline","V","voltage"},
  {"LOW_VOLTS_DISCONNECT","mdi:flash-triangle-outline","V","voltage"},
  {"DISCHARGING_LIMIT_VOLTS","mdi:flash-triangle-outline","V","voltage"},
  // ESP Data
  {"DEVICE_QUANTITY","","",""},
  {"DEVICE_FREE_HEAP","","",""},
  {"DEVICE_FREE_JSON","","",""},
  {"ESP_VCC","","V","voltage"},
  {"Wifi_RSSI","","dBm","signal_strength"},
  {"sw_version","","",""},
};