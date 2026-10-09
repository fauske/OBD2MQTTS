#include "../../include/modules/modem_client.h"
#include "config.h"
#include <ArduinoJson.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr uint32_t CMD_TIMEOUT_MS      = 2000;
constexpr uint32_t CGATT_TIMEOUT_MS    = 15000;
constexpr uint32_t MQTT_START_TIMEOUT  = 12000;
constexpr uint32_t MQTT_CONNECT_TIMEOUT = 30000;
constexpr uint32_t MQTT_PUB_TIMEOUT    = 20000;
constexpr uint32_t NET_POLL_INTERVAL   = 2000;
constexpr uint32_t NET_WAIT_MAX_MS     = 120000;
constexpr uint32_t BACKOFF_BASE_MS     = 2000;
constexpr uint32_t BACKOFF_MAX_MS      = 60000;
constexpr uint8_t  PUB_FAIL_LIMIT      = 3;

const char *state_name(ModemClient::State s) {
    switch (s) {
        case ModemClient::State::IDLE:            return "IDLE";
        case ModemClient::State::INITIALIZING:    return "INITIALIZING";
        case ModemClient::State::NETWORK_WAIT:    return "NETWORK_WAIT";
        case ModemClient::State::MQTT_CONNECTING: return "MQTT_CONNECTING";
        case ModemClient::State::READY:           return "READY";
        case ModemClient::State::ERROR:           return "ERROR";
    }
    return "?";
}

void set_f(JsonDocument &doc, const char *key, float v) {
    if (!std::isnan(v)) doc[key] = v;
}
}  // namespace

ModemClient::ModemClient()
    : _uart(SIM7080_UART_NUM == 1 ? Serial1 : Serial2) {
}

void ModemClient::begin() {
    Serial.println("[MODEM] Initializing SIM7080...");
    _uart.begin(SIM7080_BAUD, SERIAL_8N1, SIM7080_RX_PIN, SIM7080_TX_PIN);
    while (_uart.available()) _uart.read();
    _line_len = 0;
    _cmd_active = false;
    _q_count = 0;
    _fail_count = 0;
    enter(State::INITIALIZING);
}

void ModemClient::enter(State s) {
    Serial.printf("[MODEM] state %s -> %s\n", state_name(_state), state_name(s));
    _state = s;
    _step = 0;
    _step_started = false;
    _next_poll_ms = 0;
    if (s == State::NETWORK_WAIT) _net_wait_start_ms = millis();
    if (s == State::READY) {
        _pub_step = 0;
        _pub_fail = 0;
        _fail_count = 0;
    }
}

void ModemClient::fail(const char *reason) {
    _fail_count++;
    uint8_t shift = _fail_count > 6 ? 5 : _fail_count - 1;
    uint32_t delay_ms = BACKOFF_BASE_MS << shift;
    if (delay_ms > BACKOFF_MAX_MS) delay_ms = BACKOFF_MAX_MS;
    Serial.printf("[MODEM] %s (failure #%u), retry in %lu ms\n", reason, _fail_count,
                  (unsigned long)delay_ms);
    _cmd_active = false;
    _q_count = 0;  // queued data is stale after a reconnect
    _retry_at_ms = millis() + delay_ms;
    enter(State::ERROR);
}

void ModemClient::update() {
    if (_state == State::IDLE) return;
    pump();

    switch (_state) {
        case State::INITIALIZING:    run_init(); break;
        case State::NETWORK_WAIT:    run_network(); break;
        case State::MQTT_CONNECTING: run_mqtt_connect(); break;
        case State::READY:           run_publish(); break;
        case State::ERROR:
            if ((int32_t)(millis() - _retry_at_ms) >= 0) enter(State::INITIALIZING);
            break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// UART / AT response parsing
// ---------------------------------------------------------------------------

void ModemClient::pump() {
    while (_uart.available()) {
        char c = (char)_uart.read();
        if (c == '>' && _line_len == 0) {
            _cmd_got_prompt = true;
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (_line_len > 0) {
                _line[_line_len] = '\0';
                handle_line(_line);
                _line_len = 0;
            }
            continue;
        }
        if (_line_len < LINE_SIZE - 1) _line[_line_len++] = c;
    }
}

void ModemClient::handle_line(const char *line) {
    Serial.printf("[MODEM] < %s\n", line);

    // Connection loss URCs may arrive at any time.
    if (strncmp(line, "+CMQTTCONNLOST:", 15) == 0 || strncmp(line, "+CMQTTNONET", 11) == 0) {
        if (_state == State::READY) fail("MQTT connection lost");
        return;
    }
    if (!_cmd_active) return;

    if (strcmp(line, "OK") == 0) {
        _cmd_got_ok = true;
    } else if (strncmp(line, "ERROR", 5) == 0 || strncmp(line, "+CME ERROR", 10) == 0 ||
               strncmp(line, "+CMS ERROR", 10) == 0) {
        _cmd_error = true;
    } else if (_cmd_urc[0] && strncmp(line, _cmd_urc, strlen(_cmd_urc)) == 0) {
        _cmd_got_urc = true;
        const char *sep = strrchr(line, ',');
        if (!sep) sep = strchr(line, ':');
        _urc_code = sep ? atoi(sep + 1) : -1;
    } else if (line[0] == '+') {
        strncpy(_info, line, sizeof(_info) - 1);
        _info[sizeof(_info) - 1] = '\0';
    }
}

void ModemClient::start_cmd(const char *cmd, const char *urc_prefix, uint32_t timeout_ms,
                            bool wait_prompt) {
    _cmd_active = true;
    _cmd_wait_prompt = wait_prompt;
    _cmd_got_prompt = false;
    _cmd_got_ok = false;
    _cmd_got_urc = false;
    _cmd_error = false;
    _urc_code = -1;
    _info[0] = '\0';
    _cmd_urc[0] = '\0';
    if (urc_prefix) {
        strncpy(_cmd_urc, urc_prefix, sizeof(_cmd_urc) - 1);
        _cmd_urc[sizeof(_cmd_urc) - 1] = '\0';
    }
    _cmd_start_ms = millis();
    _cmd_timeout_ms = timeout_ms;
    if (cmd) {
        Serial.printf("[MODEM] > %s\n", cmd);
        _uart.print(cmd);
        _uart.print("\r\n");
    }
}

ModemClient::CmdResult ModemClient::poll_cmd() {
    if (!_cmd_active) return CmdResult::ERROR;
    CmdResult r = CmdResult::PENDING;
    if (_cmd_error) {
        r = CmdResult::ERROR;
    } else if (_cmd_wait_prompt ? _cmd_got_prompt
                                : (_cmd_got_ok && (!_cmd_urc[0] || _cmd_got_urc))) {
        r = CmdResult::OK;
    } else if (millis() - _cmd_start_ms > _cmd_timeout_ms) {
        r = CmdResult::TIMEOUT;
    }
    if (r != CmdResult::PENDING) _cmd_active = false;
    return r;
}

// Runs one AT command to completion across update() calls.
// OK: finished (optional steps report OK even on failure); ERROR: fail() was called.
ModemClient::CmdResult ModemClient::run_step(const char *cmd, const char *urc,
                                             uint32_t timeout_ms, bool optional) {
    if (!_step_started) {
        start_cmd(cmd, urc, timeout_ms);
        _step_started = true;
        return CmdResult::PENDING;
    }
    CmdResult r = poll_cmd();
    if (r == CmdResult::PENDING) return r;
    _step_started = false;
    if (r == CmdResult::OK || optional) return CmdResult::OK;

    char msg[96];
    snprintf(msg, sizeof(msg), "'%s' %s", cmd, r == CmdResult::TIMEOUT ? "timed out" : "failed");
    fail(msg);
    return CmdResult::ERROR;
}

// ---------------------------------------------------------------------------
// Connection state machine
// ---------------------------------------------------------------------------

void ModemClient::run_init() {
    CmdResult r;
    switch (_step) {
        case 0:  // basic test
            r = run_step("AT", nullptr, CMD_TIMEOUT_MS, false);
            break;
        case 1:  // echo off
            r = run_step("ATE0", nullptr, CMD_TIMEOUT_MS, false);
            break;
        case 2:  // SIM card
            r = run_step("AT+CPIN?", nullptr, CMD_TIMEOUT_MS, false);
            if (r == CmdResult::OK && !strstr(_info, "READY")) {
                fail("SIM not ready");
                return;
            }
            break;
        default:
            enter(State::NETWORK_WAIT);
            return;
    }
    if (r == CmdResult::OK) _step++;
}

void ModemClient::run_network() {
    if (millis() - _net_wait_start_ms > NET_WAIT_MAX_MS) {
        fail("network registration timeout");
        return;
    }
    if (_step == 0) {
        if (!_step_started && (int32_t)(millis() - _next_poll_ms) < 0) return;
        CmdResult r = run_step("AT+CREG?", nullptr, CMD_TIMEOUT_MS, false);
        if (r == CmdResult::PENDING || r == CmdResult::ERROR) return;
        // "+CREG: <n>,<stat>": 1 = home, 5 = roaming
        const char *comma = strrchr(_info, ',');
        int stat = comma ? atoi(comma + 1) : -1;
        if (stat == 1 || stat == 5) {
            _step = 1;
        } else {
            _next_poll_ms = millis() + NET_POLL_INTERVAL;
        }
    } else if (_step == 1) {
        CmdResult r = run_step("AT+CGATT=1", nullptr, CGATT_TIMEOUT_MS, false);
        if (r == CmdResult::OK) enter(State::MQTT_CONNECTING);
    }
}

void ModemClient::run_mqtt_connect() {
    char cmd[200];
    CmdResult r = CmdResult::PENDING;
    switch (_step) {
        // Best-effort cleanup of any previous session (errors ignored)
        case 0: r = run_step("AT+CMQTTDISC=0,5", nullptr, CMD_TIMEOUT_MS, true); break;
        case 1: r = run_step("AT+CMQTTREL=0", nullptr, CMD_TIMEOUT_MS, true); break;
        case 2: r = run_step("AT+CMQTTSTOP", nullptr, CMD_TIMEOUT_MS, true); break;
        case 3:  // start MQTT engine (+CMQTTSTART: 0, or 23 if already started)
            r = run_step("AT+CMQTTSTART", "+CMQTTSTART:", MQTT_START_TIMEOUT, false);
            if (r == CmdResult::OK && _urc_code != 0 && _urc_code != 23) {
                fail("CMQTTSTART rejected");
                return;
            }
            break;
        case 4:  // acquire client 0 (last arg: 1 = SSL/TLS)
            snprintf(cmd, sizeof(cmd), "AT+CMQTTACCQ=0,\"%s\",%d", MQTT_CLIENT_ID,
                     MQTT_USE_TLS ? 1 : 0);
            r = run_step(cmd, nullptr, CMD_TIMEOUT_MS, false);
            break;
        case 5:
            if (!MQTT_USE_TLS) { _step = 8; return; }
            r = run_step("AT+CSSLCFG=\"sslversion\",0,3", nullptr, CMD_TIMEOUT_MS, false);
            break;
        case 6:  // no server certificate verification (no CA stored on the modem)
            r = run_step("AT+CSSLCFG=\"authmode\",0,0", nullptr, CMD_TIMEOUT_MS, false);
            break;
        case 7:
            r = run_step("AT+CMQTTSSLCFG=0,0", nullptr, CMD_TIMEOUT_MS, false);
            break;
        case 8:  // connect to broker (+CMQTTCONNECT: 0,0)
            snprintf(cmd, sizeof(cmd), "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",60,1,\"%s\",\"%s\"",
                     MQTT_HOST, MQTT_PORT, MQTT_USERNAME, MQTT_PASSWORD);
            r = run_step(cmd, "+CMQTTCONNECT:", MQTT_CONNECT_TIMEOUT, false);
            if (r == CmdResult::OK && _urc_code != 0) {
                fail("MQTT connect rejected");
                return;
            }
            break;
        default:
            enter(State::READY);
            return;
    }
    if (r == CmdResult::OK) _step++;
}

// ---------------------------------------------------------------------------
// Publishing (TOPIC -> PAYLOAD -> PUB), one queued message at a time
// ---------------------------------------------------------------------------

void ModemClient::run_publish() {
    if (_q_count == 0 && !_cmd_active) return;
    PubItem &item = _queue[_q_head];
    char cmd[64];

    if (!_cmd_active) {
        switch (_pub_step) {
            case 0:
                snprintf(cmd, sizeof(cmd), "AT+CMQTTTOPIC=0,%u", (unsigned)strlen(item.topic));
                start_cmd(cmd, nullptr, CMD_TIMEOUT_MS, true);
                break;
            case 1:
                _uart.write((const uint8_t *)item.topic, strlen(item.topic));
                start_cmd(nullptr, nullptr, CMD_TIMEOUT_MS);
                break;
            case 2:
                snprintf(cmd, sizeof(cmd), "AT+CMQTTPAYLOAD=0,%u", (unsigned)item.len);
                start_cmd(cmd, nullptr, CMD_TIMEOUT_MS, true);
                break;
            case 3:
                _uart.write((const uint8_t *)item.payload, item.len);
                start_cmd(nullptr, nullptr, CMD_TIMEOUT_MS);
                break;
            case 4:
                snprintf(cmd, sizeof(cmd), "AT+CMQTTPUB=0,1,20,%d", item.retain ? 1 : 0);
                start_cmd(cmd, "+CMQTTPUB:", MQTT_PUB_TIMEOUT);
                break;
        }
        return;
    }

    CmdResult r = poll_cmd();
    if (r == CmdResult::PENDING) return;

    bool success = (r == CmdResult::OK) && (_pub_step != 4 || _urc_code == 0);
    if (!success) {
        Serial.printf("[MODEM] publish step %u failed\n", _pub_step);
        _pub_step = 0;
        _q_head = (_q_head + 1) % Q_SIZE;
        _q_count--;
        if (++_pub_fail >= PUB_FAIL_LIMIT) fail("repeated publish failures");
        return;
    }
    if (_pub_step < 4) {
        _pub_step++;
        return;
    }
    _pub_step = 0;
    _pub_fail = 0;
    _q_head = (_q_head + 1) % Q_SIZE;
    _q_count--;
}

bool ModemClient::enqueue(const char *topic, const char *payload, size_t len, bool retain) {
    if (_state != State::READY || _q_count >= Q_SIZE) return false;
    if (len == 0 || len >= sizeof(PubItem::payload)) return false;
    PubItem &slot = _queue[(_q_head + _q_count) % Q_SIZE];
    slot.topic = topic;
    memcpy(slot.payload, payload, len);
    slot.payload[len] = '\0';
    slot.len = len;
    slot.retain = retain;
    _q_count++;
    return true;
}

// ---------------------------------------------------------------------------
// JSON payloads
// ---------------------------------------------------------------------------

static bool serialize_and_queue(JsonDocument &doc, char *buf, size_t cap, size_t *len) {
    if (doc.overflowed()) return false;
    *len = serializeJson(doc, buf, cap);
    return *len > 0 && *len < cap;
}

bool ModemClient::publish_telemetry(const BmsData &bms) {
    if (_state != State::READY) return false;
    JsonDocument doc;
    doc["ts"] = millis();
    set_f(doc, "soc_bms", bms.soc_bms);
    set_f(doc, "soc_display", bms.soc_display);
    set_f(doc, "pack_v", bms.pack_v);
    set_f(doc, "pack_a", bms.pack_a);
    set_f(doc, "aux_v", bms.aux_v);
    set_f(doc, "cell_v_max", bms.cell_v_max);
    set_f(doc, "cell_v_min", bms.cell_v_min);
    set_f(doc, "temp_max", bms.temp_max);
    set_f(doc, "temp_min", bms.temp_min);
    set_f(doc, "temp_inlet", bms.temp_inlet);
    set_f(doc, "soh", bms.soh);
    set_f(doc, "avail_chg_kw", bms.avail_chg_kw);
    set_f(doc, "avail_dis_kw", bms.avail_dis_kw);
    doc["charging"] = bms.charging;

    char payload[sizeof(PubItem::payload)];
    size_t n;
    if (!serialize_and_queue(doc, payload, sizeof(payload), &n)) return false;
    return enqueue(MQTT_TOPIC_TELEMETRY, payload, n, false);
}

bool ModemClient::publish_power(const PowerData &power) {
    if (_state != State::READY) return false;
    JsonDocument doc;
    doc["ts"] = millis();
    set_f(doc, "car_12v", power.car_12v);
    set_f(doc, "board_v", power.board_v);

    char payload[sizeof(PubItem::payload)];
    size_t n;
    if (!serialize_and_queue(doc, payload, sizeof(payload), &n)) return false;
    return enqueue(MQTT_TOPIC_STATUS, payload, n, false);
}

bool ModemClient::publish_status(const SystemStatus &status) {
    if (_state != State::READY) return false;
    JsonDocument doc;
    doc["ts"] = millis();
    doc["ble_connected"] = status.ble_connected;
    doc["obd_ready"] = status.obd_ready;
    doc["mqtt_online"] = status.mqtt_online;
    doc["rssi"] = status.cell_rssi;
    doc["uptime_s"] = status.uptime_s;

    char payload[sizeof(PubItem::payload)];
    size_t n;
    if (!serialize_and_queue(doc, payload, sizeof(payload), &n)) return false;
    return enqueue(MQTT_TOPIC_STATUS, payload, n, true);
}
