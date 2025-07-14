#include "postman_mqtt.h"

// Basic constructor
PostmanMQTT::PostmanMQTT(PubSubClient& client, const char* board_id) : _client(client), _board_id(board_id) {}

// Setup MQTT server and callback
void PostmanMQTT::setup(const char* server, int port, std::function<void(char*, uint8_t*, unsigned int)> callback, uint16_t buffer_size) {
    _client.setServer(server, port);
    _client.setCallback(callback);
    _client.setBufferSize(buffer_size);
}

// Handle MQTT connection loop
void PostmanMQTT::loop() {
    if (!_client.connected()) {
        reconnect();
    }
    _client.loop();
}

// Publish a message to a topic
void PostmanMQTT::publish(const char* topic, const JsonDocument& doc) {
    char buffer[4096];
    serializeJson(doc, buffer);
    String full_topic = "pocketlab/" + _board_id + "/" + topic;
    _client.publish(full_topic.c_str(), buffer);
}

// Subscribe to a topic
void PostmanMQTT::subscribe(const char* topic) {
    String full_topic = "pocketlab/" + _board_id + "/" + topic;
    _client.subscribe(full_topic.c_str());
}

void PostmanMQTT::sendStatus(const char* device_status, const char* current_mode, float progress) {
    JsonDocument doc;

    // Get timestamp and message_id
    // Note: This is a simplified timestamp. For true ISO8601, a library would be needed.
    char timestamp[30];
    snprintf(timestamp, sizeof(timestamp), "%lu", millis()); 

    doc["timestamp"] = timestamp;
    doc["message_id"] = "status-" + String(millis());
    doc["type"] = "status";

    JsonObject payload = doc["payload"].to<JsonObject>();
    payload["device_status"] = device_status;
    payload["current_mode"] = current_mode;
    if (progress >= 0.0) {
        payload["progress"] = progress;
    }

    publish("status", doc);
}

void PostmanMQTT::sendResponse(const char* mode, const char* status, const char* message, int estimated_duration) {
    JsonDocument doc;

    char timestamp[30];
    snprintf(timestamp, sizeof(timestamp), "%lu", millis());

    doc["timestamp"] = timestamp;
    doc["message_id"] = "response-" + String(millis());
    doc["type"] = "response";

    JsonObject payload = doc["payload"].to<JsonObject>();
    payload["mode"] = mode;
    payload["status"] = status;
    payload["message"] = message;
    if (estimated_duration >= 0) {
        payload["estimated_duration"] = estimated_duration;
    }

    publish("response", doc);
}

void PostmanMQTT::sendError(const char* error_code, const char* error_message, const char* context_mode, const char* context_parameter, const char* context_value, const char* suggested_action) {
    JsonDocument doc;

    char timestamp[30];
    snprintf(timestamp, sizeof(timestamp), "%lu", millis());

    doc["timestamp"] = timestamp;
    doc["message_id"] = "error-" + String(millis());
    doc["type"] = "status"; // Error messages are a type of status message

    JsonObject payload = doc["payload"].to<JsonObject>();
    payload["error"] = true;
    payload["error_code"] = error_code;
    payload["error_message"] = error_message;

    JsonObject error_context = payload["error_context"].to<JsonObject>();
    error_context["mode"] = context_mode;
    error_context["parameter"] = context_parameter;
    error_context["value"] = context_value;

    payload["suggested_action"] = suggested_action;

    publish("status", doc);
}

// Reconnect to the MQTT broker
void PostmanMQTT::reconnect() {
    while (!_client.connected()) {
        Serial.print("Attempting MQTT connection...");
        if (_client.connect("PocKETlabClient")) {
            Serial.println("connected");
            subscribe("command");
        } else {
            Serial.print("failed, rc=");
            Serial.print(_client.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}
