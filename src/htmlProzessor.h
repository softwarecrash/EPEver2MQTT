//#include <Arduino.h>
String htmlProcessor(const String &var)
{
    extern Settings _settings;
    if (var == F("pre_head_template"))
        return (HTML_HEAD);
    if (var == F("pre_foot_template"))
        return (HTML_FOOT);
    if (var == F("pre_software_version"))
        return (SOFTWARE_VERSION);
    if (var == F("pre_swversion"))
        return (SWVERSION);
   if (var == F("pre_device_name"))
        return (_settings._deviceName);
    if (var == ("pre_device_quantity"))
        return (String(_settings._deviceQuantity).c_str());
    if (var == F("pre_mqtt_server"))
        return (_settings._mqttServer);
    if (var == F("pre_mqtt_port"))
        return (String(_settings._mqttPort).c_str());
    if (var == F("pre_mqtt_user"))
        return (_settings._mqttUser);
    if (var == F("pre_mqtt_pass"))
        return (_settings._mqttPassword);
    if (var == F("pre_mqtt_topic"))
        return (_settings._mqttTopic);
    if (var == F("pre_mqtt_refresh"))
        return (String(_settings._mqttRefresh).c_str());
    if (var == F("pre_mqtt_json"))
        return (_settings._mqttJson ? "checked":"");
    return String();
}
