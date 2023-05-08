#define ARDUINOJSON_USE_DOUBLE 0
#define ARDUINOJSON_USE_LONG_LONG 0

#define MYPORT_TX 12
#define MYPORT_RX 13
#define EPEVER_BAUD 115200   // baud rate for modbus
#define EPEVER_DE_RE 5       // connect DE and Re to pin D1

bool getEpData(int invNum);

bool getJsonData(int invNum);

void callback(char *top, byte *payload, unsigned int length);

bool sendtoMQTT(int invNum);