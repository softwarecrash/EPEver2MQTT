#define ARDUINOJSON_USE_DOUBLE 0
#define ARDUINOJSON_USE_LONG_LONG 1

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
  {"CONNECTION","connection","",""},
  //{"DEVICE_NUM","numeric","",""},
  //{"DEVICE_TIME","timer-outline","",""},
  {"DEVICE_TEMP","thermometer-lines","°C","temperature"},
  {"SOLAR_V","solar-power","V","voltage"},
  {"SOLAR_A","solar-power","A","current"},
  {"SOLAR_W","solar-power","W","power"},
  {"BATT_SOC","car-battery","%","battery"},
  {"BATT_V","battery","V","voltage"},
  {"BATT_A","battery","A","current"},
  {"BATT_W","battery","W","power"},
  {"BATT_STATE","state-machine","",""},
  {"BATT_TEMP","thermometer-lines","°C","temperature"},
  {"BATT_TEMP_STATE","thermometer-lines","",""},
  {"LOAD_V","battery-charging-medium","V","voltage"},
  {"LOAD_A","battery-charging-medium","A","current"},
  {"LOAD_W","battery-charging-medium","W","power"},
  {"LOAD_STATE","power-socket-us","",""},
  {"CHARGER_STATE","ev-station","",""},
  {"CHARGER_MODE","ev-station","",""},
  // StatsData
  {"SOLAR_MAX","sun-angle","V","voltage"},
  {"SOLAR_MIN","sun-angle-outline","V","voltage"},
  {"BATT_MAX","battery-arrow-up-outline","V","voltage"},
  {"BATT_MIN","battery-arrow-down-outline","V","voltage"},
  {"CONS_DAY","solar-power-variant","Wh","energy"},
  {"CONS_MON","solar-power-variant","Wh","energy"},
  {"CONS_YEAR","solar-power-variant","Wh","energy"},
  {"CONS_TOT","solar-power-variant","Wh","energy"},
  {"GEN_DAY","solar-power","Wh","energy"},
  {"GEN_MON","solar-power","Wh","energy"},
  {"GEN_YEAR","solar-power","Wh","energy"},
  {"GEN_TOT","solar-power","Wh","energy"},
  {"CO2_REDUCTION","molecule-co","",""},
  // DeviceData
  {"BATTERY_TYPE","fuel-cell","",""},
  {"BATTERY_CAPACITY","car-battery","Wh","energy_storage"},
  {"TEMPERATURE_COMPENSATION","thermometer-lines","mV","voltage"},
  {"HIGH_VOLT_DISCONNECT","flash-triangle-outline","V","voltage"},
  {"CHARGING_LIMIT_VOLTS","battery-minus-outline","V","voltage"},
  {"OVER_VOLTS_RECONNECT","battery-sync-outline","V","voltage"},
  {"EQUALIZATION_VOLTS","battery-sync-outline","V","voltage"},
  {"BOOST_VOLTS","battery-plus","V","voltage"},
  {"FLOAT_VOLTS","battery-plus-variant","V","voltage"},
  {"BOOST_RECONNECT_VOLTS","battery-plus","V","voltage"},
  {"LOW_VOLTS_RECONNECT","flash-triangle-outline","V","voltage"},
  {"UNDER_VOLTS_RECOVER","flash-triangle-outline","V","voltage"},
  {"UNDER_VOLTS_WARNING","flash-triangle-outline","V","voltage"},
  {"LOW_VOLTS_DISCONNECT","flash-triangle-outline","V","voltage"},
  {"DISCHARGING_LIMIT_VOLTS","flash-triangle-outline","V","voltage"},
  // ESP Data
  //{"DEVICE_QUANTITY","chip","",""},
  //{"DEVICE_FREE_HEAP","chip","",""},
  //{"DEVICE_FREE_JSON","chip","",""},
  {"ESP_VCC","flash-triangle-outline","V","voltage"},
  {"Wifi_RSSI","wifi-arrow-up-down","dBm","signal_strength"},
  //{"sw_version","chip","",""},
};
