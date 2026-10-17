#include "../../include/modules/modem_client.h"
#include "config.h"
#include <string.h>

ModemClient::ModemClient()
    : _mqtt_client(_wifi_client) {
}

void ModemClient::begin() {
    Serial.println("[MODEM] Initializing LTE client...");
    _wifi_client.setInsecure();
    _mqtt_client.setServer(MQTT_HOST, MQTT_PORT);
    _mqtt_client.setBufferSize(512);
    _state = State::INITIALIZING;
    _init_step = 0;
}

void ModemClient::update() {
    switch (_state) {
        case State::INITIALIZING:
            do_init();
            break;
        case State::WAITING_NETWORK:
            do_connect();
            break;
        case State::READY:
            if (!_mqtt_client.connected()) {
                if (millis() - _last_reconnect_ms > 5000) {
                    _last_reconnect_ms = millis();
                    connect_mqtt();
                }
            }
            _mqtt_client.loop();
            break;
        default:
            break;
    }
}

void ModemClient::do_init() {
    // LTE/MQTT init placeholder; treat as connected once we reach READY.
    if (_init_step < 2) {
        _state = State::WAITING_NETWORK;
        _init_step = 2;
        Serial.println("[MODEM] LTE modem init complete");
    }
}

void ModemClient::do_connect() {
    if (connect_mqtt()) {
        _state = State::READY;
        Serial.println("[MODEM] LTE MQTT ready");
    }
}

bool ModemClient::connect_mqtt() {
    if (_mqtt_client.connected()) return true;

    if (_mqtt_client.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
        return true;
    }

    Serial.printf("[MODEM] MQTT connect failed: rc=%d\n", _mqtt_client.state());
    return false;
}

void ModemClient::send_at_command(const char *cmd) {
    (void)cmd;
}

bool ModemClient::read_response(uint32_t timeout_ms) {
    (void)timeout_ms;
    return true;
}

bool ModemClient::publish_telemetry(const BmsData &bms) {
    if (!_mqtt_client.connected()) return false;

    DynamicJsonDocument doc(256);
    doc["ts"] = millis();
    doc["soc_bms"] = bms.soc_bms;
    doc["soc_display"] = bms.soc_display;
    doc["pack_v"] = bms.pack_v;
    doc["pack_a"] = bms.pack_a;
    doc["aux_v"] = bms.aux_v;
    doc["charging"] = bms.charging;

    char payload[256];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n == 0) return false;

    return _mqtt_client.publish(MQTT_TOPIC_TELEMETRY, payload, false);
}

bool ModemClient::publish_power(const PowerData &power) {
    if (!_mqtt_client.connected()) return false;

    DynamicJsonDocument doc(256);
    doc["ts"] = millis();
    doc["car_12v"] = power.car_12v;
    doc["board_v"] = power.board_v;

    char payload[256];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n == 0) return false;

    return _mqtt_client.publish(MQTT_TOPIC_STATUS, payload, false);
}

bool ModemClient::publish_status(const SystemStatus &status) {
    if (!_mqtt_client.connected()) return false;

    DynamicJsonDocument doc(256);
    doc["ts"] = millis();
    doc["ble_connected"] = status.ble_connected;
    doc["obd_ready"] = status.obd_ready;
    doc["mqtt_online"] = status.mqtt_online;
    doc["rssi"] = status.cell_rssi;
    doc["uptime_s"] = status.uptime_s;

    char payload[256];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n == 0) return false;

    return _mqtt_client.publish(MQTT_TOPIC_STATUS, payload, true);
}
