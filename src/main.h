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
  {"CONNECTION","","",""},
  {"DEVICE_NUM","","",""},
  {"DEVICE_TIME","","",""},
  {"DEVICE_TEMP","","",""},
  {"SOLAR_V","","V","voltage"},
  {"SOLAR_A","","A","current"},
  {"SOLAR_W","","W","power"},
  {"BATT_SOC","mdi:battery-charging-high","%","battery"},
  {"BATT_V","","V","voltage"},
  {"BATT_A","","A","current"},
  {"BATT_W","","W","power"},
  {"BATT_STATE","","",""},
  {"BATT_TEMP","mdi:thermometer-lines","°C","temperature"},
  {"BATT_TEMP_STATE","","",""},
  {"LOAD_V","","V","voltage"},
  {"LOAD_A","","A","current"},
  {"LOAD_W","","W","power"},
  {"LOAD_STATE","","",""},
  {"CHARGER_STATE","","",""},
  {"CHARGER_MODE","","",""},
  // StatsData
  {"SOLAR_MAX","","",""},
  {"SOLAR_MIN","","",""},
  {"BATT_MAX","","",""},
  {"BATT_MIN","","",""},
  {"CONS_DAY","","",""},
  {"CONS_MON","","",""},
  {"CONS_YEAR","","",""},
  {"CONS_TOT","","",""},
  {"GEN_DAY","","",""},
  {"GEN_MON","","",""},
  {"GEN_YEAR","","",""},
  {"GEN_TOT","","",""},
  {"CO2_REDUCTION","","",""},
  // DeviceData
  {"BATTERY_TYPE","","",""},
  {"BATTERY_CAPACITY","","",""},
  {"TEMPERATURE_COMPENSATION","","",""},
  {"HIGH_VOLT_DISCONNECT","","V","voltage"},
  {"CHARGING_LIMIT_VOLTS","","V","voltage"},
  {"OVER_VOLTS_RECONNECT","","V","voltage"},
  {"EQUALIZATION_VOLTS","","V","voltage"},
  {"BOOST_VOLTS","","V","voltage"},
  {"FLOAT_VOLTS","","V","voltage"},
  {"BOOST_RECONNECT_VOLTS","","V","voltage"},
  {"LOW_VOLTS_RECONNECT","","V","voltage"},
  {"UNDER_VOLTS_RECOVER","","V","voltage"},
  {"UNDER_VOLTS_WARNING","","V","voltage"},
  {"LOW_VOLTS_DISCONNECT","","V","voltage"},
  {"DISCHARGING_LIMIT_VOLTS","","V","voltage"},
  // ESP Data
  {"DEVICE_QUANTITY","","",""},
  {"DEVICE_FREE_HEAP","","",""},
  {"DEVICE_FREE_JSON","","",""},
  {"ESP_VCC","","",""},
  {"Wifi_RSSI","","",""},
  {"sw_version","","",""},
};