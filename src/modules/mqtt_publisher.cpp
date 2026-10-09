#include "../../include/modules/mqtt_publisher.h"
#include "../../config.h"

// ============================================================================
// MQTT Publisher Implementation (MQTTS with TLS)
// ============================================================================

MqttPublisher::MqttPublisher()
    : _mqtt_client(_wifi_client) {
}

void MqttPublisher::begin() {
    Serial.println("[MQTT] Initializing MQTTS publisher...");

#if MQTT_USE_TLS
    _wifi_client.setInsecure();  // For now, skip cert verification
    // In production, use: _wifi_client.setCACert(MQTT_CA_CERT);
#endif

    _mqtt_client.setServer(MQTT_HOST, MQTT_PORT);
    _mqtt_client.setBufferSize(512);
}

void MqttPublisher::update() {
    if (!_mqtt_client.connected()) {
        if (millis() - _last_reconnect_ms > 5000) {
            _last_reconnect_ms = millis();
            connect();
        }
    } else {
        _mqtt_client.loop();

        // Periodic ping
        if (millis() - _last_ping_ms > 60000) {
            _last_ping_ms = millis();
            // PubSubClient handles MQTT keep-alive internally
        }
    }
}

void MqttPublisher::connect() {
    Serial.println("[MQTT] Connecting to broker...");

#if MQTT_USE_TLS
    Serial.printf("[MQTT] Connecting to %s:%d (MQTTS)\n", MQTT_HOST, MQTT_PORT);
#else
    Serial.printf("[MQTT] Connecting to %s:%d (plain MQTT)\n", MQTT_HOST, MQTT_PORT);
#endif

    if (_mqtt_client.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
        Serial.println("[MQTT] Connected");
        _connected = true;
        _last_ping_ms = millis();
    } else {
        Serial.printf("[MQTT] Connection failed, rc=%d\n", _mqtt_client.state());
        _connected = false;
    }
}

bool MqttPublisher::publish_telemetry(const BmsData &bms) {
    if (!_connected) return false;

    DynamicJsonDocument doc(256);
    doc["ts"] = millis();
    doc["soc_bms"] = bms.soc_bms;
    doc["soc_display"] = bms.soc_display;
    doc["pack_v"] = bms.pack_v;
    doc["pack_a"] = bms.pack_a;
    doc["aux_v"] = bms.aux_v;
    doc["charging"] = bms.charging;
    doc["dc_charging"] = bms.dc_charging;

    char payload[256];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n > 0 && n < sizeof(payload)) {
        bool success = _mqtt_client.publish(MQTT_TOPIC_TELEMETRY, payload);
        if (success) {
            Serial.printf("[MQTT] Published telemetry (%u bytes)\n", n);
        }
        return success;
    }
    return false;
}

bool MqttPublisher::publish_status(const SystemStatus &status) {
    if (!_connected) return false;

    DynamicJsonDocument doc(256);
    doc["ts"] = millis();
    doc["ble_connected"] = status.ble_connected;
    doc["obd_ready"] = status.obd_ready;
    doc["mqtt_online"] = status.mqtt_online;
    doc["rssi"] = status.cell_rssi;
    doc["uptime_s"] = status.uptime_s;

    char payload[256];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n > 0 && n < sizeof(payload)) {
        bool success = _mqtt_client.publish(MQTT_TOPIC_STATUS, payload, true);
        if (success) {
            Serial.printf("[MQTT] Published status (%u bytes)\n", n);
        }
        return success;
    }
    return false;
}

bool MqttPublisher::publish_location(const GnssData &gnss) {
    if (!_connected || !gnss.valid) return false;

    DynamicJsonDocument doc(256);
    doc["ts"] = millis();
    doc["lat"] = gnss.lat;
    doc["lon"] = gnss.lon;
    doc["hdop"] = gnss.hdop;

    char payload[256];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n > 0 && n < sizeof(payload)) {
        bool success = _mqtt_client.publish(MQTT_TOPIC_TELEMETRY, payload, true);
        return success;
    }
    return false;
}

void MqttPublisher::handle_message(char *topic, uint8_t *payload, unsigned int length) {
    Serial.printf("[MQTT] Message on %s\n", topic);
}
