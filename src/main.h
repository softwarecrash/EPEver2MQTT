

bool getEpData(int invNum);

bool getJsonData(int invNum);

void callback(char *top, byte *payload, unsigned int length);

bool sendtoMQTT(int invNum);