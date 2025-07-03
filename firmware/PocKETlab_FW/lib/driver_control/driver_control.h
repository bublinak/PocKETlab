#ifndef DRIVER_CONTROL_H
#define DRIVER_CONTROL_H

#include <ArduinoJson.h>
#include "postman_mqtt.h"
#include "pocketlab_io.h"

// Basic structure for Driver Control library
class DriverControl {
public:
    DriverControl(PostmanMQTT& postman, PocKETlabIO& io);
    ~DriverControl();

    void handleCommand(const JsonDocument& doc);
    void loop();

private:
    PostmanMQTT& _postman;
    PocKETlabIO& _io;
    bool _testbed_running;
    unsigned long _testbed_last_update;
    int _testbed_update_interval;

    void handleVA(JsonObjectConst settings);
    void handleBode(JsonObjectConst settings);
    void handleStep(JsonObjectConst settings);
    void handleImpulse(JsonObjectConst settings);
    void handleTestbed(JsonObjectConst settings);
    void handleControlSystem(JsonObjectConst settings);
};

#endif // DRIVER_CONTROL_H
