#pragma once

#include "../data_types.h"
#include <Arduino.h>
#include <ArduinoJson.h>

// SIM7080G LTE-M/NB-IoT client: MQTT(S) over AT commands. No WiFi involved.
class ModemClient {
public:
    enum class State : uint8_t {
        IDLE,
        INITIALIZING,
        WAITING_NETWORK,
        CONNECTING_MQTT,
        READY,
        POWERING_OFF,
        OFF,
        ERROR
    };

    ModemClient();

    void begin();
    void update();
    bool is_online() const { return _state == State::READY; }
    bool is_busy() const { return _tx_in_flight; }
    // Connected with nothing queued or in flight.
    bool is_idle() const { return _state == State::READY && _q_count == 0 && _ready_step == 0; }
    bool is_off() const { return _state == State::OFF; }
    // Gracefully powers the modem down (AT+CPOWD); poll is_off() afterwards.
    void request_power_off();
    int8_t rssi() const { return _rssi; }

    bool publish_telemetry(const BmsData &bms);
    bool publish_power(const PowerData &power);
    bool publish_status(const SystemStatus &status);

    State state() const { return _state; }

private:
    enum class Rx : uint8_t { PENDING, OK, FAIL };

    static constexpr size_t RX_BUF_SIZE = 512;
    static constexpr size_t PAYLOAD_SIZE = 256;
    static constexpr uint8_t QUEUE_SIZE = 3;

    struct Pending {
        const char *topic = nullptr;
        char payload[PAYLOAD_SIZE] = {};
        uint16_t len = 0;
        bool retain = false;
    };

    State _state = State::IDLE;

    bool _tx_in_flight = false;
    uint32_t _cmd_sent_ms = 0;
    uint32_t _step_ms = 0;
    uint32_t _error_ms = 0;
    uint32_t _net_start_ms = 0;
    uint32_t _last_check_ms = 0;
    uint8_t _init_step = 0;
    uint8_t _net_step = 0;
    uint8_t _mqtt_step = 0;
    uint8_t _ready_step = 0;
    uint8_t _retries = 0;
    int8_t _rssi = 0;

    char _rx_buf[RX_BUF_SIZE] = {};
    size_t _rx_len = 0;

    Pending _queue[QUEUE_SIZE];
    uint8_t _q_head = 0;
    uint8_t _q_count = 0;

    void do_init();
    void do_connect();
    void do_mqtt();
    void do_ready();

    void fail(const char *what);
    void parse_rssi();
    bool enqueue(const char *topic, const char *payload, size_t len, bool retain);

    void send_at_command(const char *cmd);
    Rx read_response(const char *expect, uint32_t timeout_ms);
    Rx exec(const char *cmd, const char *expect, uint32_t timeout_ms);
    // Runs one command; advances `step` on OK, enters ERROR on failure (or advances anyway if lenient).
    bool run_step(uint8_t &step, const char *cmd, const char *expect, uint32_t timeout_ms,
                  bool lenient = false);
};
