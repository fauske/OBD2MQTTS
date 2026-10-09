#pragma once

#include "../data_types.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

class ModemClient {
public:
    enum class State : uint8_t {
        IDLE,
        INITIALIZING,
        WAITING_NETWORK,
        CONNECTING_MQTT,
        READY,
        ERROR
    };

    ModemClient();

    void begin();
    void update();
    bool is_online() const { return _state == State::READY; }
    bool is_busy() const { return _tx_in_flight; }

    bool publish_telemetry(const BmsData &bms);
    bool publish_power(const PowerData &power);
    bool publish_status(const SystemStatus &status);

    State state() const { return _state; }

private:
    State _state = State::IDLE;

    WiFiClientSecure _wifi_client;
    PubSubClient _mqtt_client;

    bool _tx_in_flight = false;
    uint32_t _cmd_sent_ms = 0;
    uint8_t _init_step = 0;
    uint32_t _last_reconnect_ms = 0;

    static constexpr size_t RX_BUF_SIZE = 512;
    char _rx_buf[RX_BUF_SIZE] = {};
    size_t _rx_len = 0;
    char _response[RX_BUF_SIZE] = {};

    void do_init();
    void do_connect();
    bool connect_mqtt();
    void send_at_command(const char *cmd);
    bool read_response(uint32_t timeout_ms);
};
