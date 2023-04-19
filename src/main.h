#define ARDUINOJSON_USE_DOUBLE 0
#define ARDUINOJSON_USE_LONG_LONG 0

bool getEpData(int invNum);

bool getJsonData(int invNum);

void callback(char *top, byte *payload, unsigned int length);

bool sendtoMQTT(int invNum);