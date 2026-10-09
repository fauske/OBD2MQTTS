#pragma once

#include "../data_types.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ============================================================================
// MQTT Publisher for telemetry over MQTTS (MQTT + TLS)
// ============================================================================

class MqttPublisher {
public:
    MqttPublisher();

    void  begin();
    void  update();
    bool  is_online() const { return _connected; }

    bool  publish_telemetry(const BmsData &bms);
    bool  publish_status(const SystemStatus &status);
    bool  publish_location(const GnssData &gnss);

private:
    WiFiClientSecure _wifi_client;
    PubSubClient _mqtt_client;
    bool     _connected = false;
    uint32_t _last_reconnect_ms = 0;
    uint32_t _last_ping_ms = 0;

    void connect();
    void handle_message(char *topic, uint8_t *payload, unsigned int length);
};
