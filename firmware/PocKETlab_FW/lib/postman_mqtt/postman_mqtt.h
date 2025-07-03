#ifndef POSTMAN_MQTT_H
#define POSTMAN_MQTT_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

class PostmanMQTT {
public:
    PostmanMQTT(PubSubClient& client, const char* board_id);
    void setup(const char* server, int port, std::function<void(char*, uint8_t*, unsigned int)> callback, uint16_t buffer_size = 2048);
    void loop();
    void publish(const char* topic, const JsonDocument& doc);
    void subscribe(const char* topic);
    void sendStatus(const char* device_status, const char* current_mode, float progress = -1.0f);
    void sendResponse(const char* mode, const char* status, const char* message, int estimated_duration = -1);
    void sendError(const char* error_code, const char* error_message, const char* context_mode, const char* context_parameter, const char* context_value, const char* suggested_action);

private:
    PubSubClient& _client;
    String _board_id;
    void reconnect();
};

#endif // POSTMAN_MQTT_H
