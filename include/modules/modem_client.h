#pragma once

#include "../data_types.h"
#include <Arduino.h>
#include <HardwareSerial.h>

// ============================================================================
// SIM7080 LTE modem client: MQTT(S) over AT commands, fully non-blocking.
// State: IDLE -> INITIALIZING -> NETWORK_WAIT -> MQTT_CONNECTING -> READY
// ============================================================================

class ModemClient {
public:
    enum class State : uint8_t {
        IDLE,
        INITIALIZING,
        NETWORK_WAIT,
        MQTT_CONNECTING,
        READY,
        ERROR          // backing off before a retry
    };

    ModemClient();

    void begin();
    void update();
    bool is_online() const { return _state == State::READY; }
    bool is_busy() const { return _cmd_active || _q_count > 0; }

    bool publish_telemetry(const BmsData &bms);
    bool publish_power(const PowerData &power);
    bool publish_status(const SystemStatus &status);

    State state() const { return _state; }

private:
    enum class CmdResult : uint8_t { PENDING, OK, ERROR, TIMEOUT };

    struct PubItem {
        const char *topic = nullptr;
        char payload[384] = {};
        size_t len = 0;
        bool retain = false;
    };

    static constexpr size_t LINE_SIZE = 128;
    static constexpr size_t Q_SIZE = 3;

    State _state = State::IDLE;
    HardwareSerial &_uart;

    // Line assembly
    char _line[LINE_SIZE] = {};
    size_t _line_len = 0;

    // Active AT command
    bool _cmd_active = false;
    bool _cmd_wait_prompt = false;
    bool _cmd_got_prompt = false;
    bool _cmd_got_ok = false;
    bool _cmd_got_urc = false;
    bool _cmd_error = false;
    char _cmd_urc[24] = {};
    int _urc_code = -1;
    uint32_t _cmd_start_ms = 0;
    uint32_t _cmd_timeout_ms = 0;
    char _info[LINE_SIZE] = {};   // last "+XXX:" info line that is not the URC

    // State-machine bookkeeping
    uint8_t _step = 0;
    bool _step_started = false;
    uint8_t _fail_count = 0;
    uint32_t _retry_at_ms = 0;
    uint32_t _next_poll_ms = 0;
    uint32_t _net_wait_start_ms = 0;

    // Publish queue + sequence
    PubItem _queue[Q_SIZE];
    uint8_t _q_head = 0;
    uint8_t _q_count = 0;
    uint8_t _pub_step = 0;
    uint8_t _pub_fail = 0;

    void pump();
    void handle_line(const char *line);
    void start_cmd(const char *cmd, const char *urc_prefix, uint32_t timeout_ms,
                   bool wait_prompt = false);
    CmdResult poll_cmd();

    void run_init();
    void run_network();
    void run_mqtt_connect();
    void run_publish();

    CmdResult run_step(const char *cmd, const char *urc, uint32_t timeout_ms, bool optional);
    void fail(const char *reason);
    void enter(State s);
    bool enqueue(const char *topic, const char *payload, size_t len, bool retain);
};
