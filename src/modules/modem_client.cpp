#include "../../include/modules/modem_client.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr uint32_t NET_REG_TIMEOUT_MS = 180000;
constexpr uint32_t ERROR_RETRY_MS = 30000;
constexpr uint32_t CHECK_INTERVAL_MS = MODEM_LINK_CHECK_INTERVAL_MS;
}

ModemClient::ModemClient() {
}

void ModemClient::begin() {
    if (!ENABLE_SIM7080) return;

    Serial.println("[MODEM] Initializing LTE client...");
    pinMode(SIM7080_DTR_PIN, OUTPUT);
    digitalWrite(SIM7080_DTR_PIN, LOW);
    pinMode(SIM7080_PWRKEY_PIN, OUTPUT);
    digitalWrite(SIM7080_PWRKEY_PIN, LOW);

    Serial1.setRxBufferSize(1024);
    Serial1.begin(SIM7080_BAUD_RATE, SERIAL_8N1, SIM7080_RX_PIN, SIM7080_TX_PIN);

    _state = State::INITIALIZING;
    _init_step = 0;
    _retries = 0;
}

void ModemClient::update() {
    switch (_state) {
        case State::INITIALIZING:     do_init(); break;
        case State::WAITING_NETWORK:  do_connect(); break;
        case State::CONNECTING_MQTT:  do_mqtt(); break;
        case State::READY:            do_ready(); break;
        case State::POWERING_OFF: {
            Rx r = exec("AT+CPOWD=1", "POWER DOWN", 8000);
            if (r != Rx::PENDING) {
                _tx_in_flight = false;
                _state = State::OFF;
                Serial.println("[MODEM] Powered off");
            }
            break;
        }
        case State::ERROR:
            if (millis() - _error_ms >= ERROR_RETRY_MS) {
                Serial.println("[MODEM] Retrying modem init");
                _state = State::INITIALIZING;
                _init_step = 0;
                _retries = 0;
            }
            break;
        default:
            break;
    }
}

void ModemClient::request_power_off() {
    _tx_in_flight = false;
    _q_count = 0;
    _ready_step = 0;
    if (_state == State::IDLE || _state == State::OFF) {
        _state = State::OFF;
        return;
    }
    _state = State::POWERING_OFF;
}

void ModemClient::fail(const char *what) {
    Serial.printf("[MODEM] %s failed\n", what);
    _tx_in_flight = false;
    _q_count = 0;
    _state = State::ERROR;
    _error_ms = millis();
}

// ---------------------------------------------------------------------------
// AT transport
// ---------------------------------------------------------------------------

void ModemClient::send_at_command(const char *cmd) {
    while (Serial1.available()) Serial1.read();
    _rx_len = 0;
    _rx_buf[0] = '\0';

    // Credentials travel in AT+SMCONF lines, so those are never logged.
    if (strncmp(cmd, "AT+SMCONF", 9) != 0) {
        Serial.printf("[MODEM] > %s\n", cmd);
    }
    Serial1.print(cmd);
    Serial1.print("\r\n");
    _cmd_sent_ms = millis();
    _tx_in_flight = true;
}

ModemClient::Rx ModemClient::read_response(const char *expect, uint32_t timeout_ms) {
    while (Serial1.available() && _rx_len < RX_BUF_SIZE - 1) {
        _rx_buf[_rx_len++] = (char)Serial1.read();
    }
    _rx_buf[_rx_len] = '\0';

    if (strstr(_rx_buf, expect)) return Rx::OK;
    if (strstr(_rx_buf, "ERROR")) return Rx::FAIL;
    if (millis() - _cmd_sent_ms > timeout_ms) return Rx::FAIL;
    return Rx::PENDING;
}

// Sends `cmd` on the first call, then polls. A null `cmd` only polls an already-sent command.
ModemClient::Rx ModemClient::exec(const char *cmd, const char *expect, uint32_t timeout_ms) {
    if (!_tx_in_flight) {
        send_at_command(cmd);
        return Rx::PENDING;
    }
    Rx r = read_response(expect, timeout_ms);
    if (r != Rx::PENDING) _tx_in_flight = false;
    return r;
}

bool ModemClient::run_step(uint8_t &step, const char *cmd, const char *expect,
                           uint32_t timeout_ms, bool lenient) {
    Rx r = exec(cmd, expect, timeout_ms);
    if (r == Rx::OK || (r == Rx::FAIL && lenient)) {
        step++;
        return true;
    }
    if (r == Rx::FAIL) {
        // SMCONF lines carry credentials; never print them or their responses.
        bool secret = cmd && strncmp(cmd, "AT+SMCONF", 9) == 0;
        if (!secret) Serial.printf("[MODEM] response: %s\n", _rx_buf);
        fail(cmd && !secret ? cmd : "AT command/step");
    }
    return false;
}

void ModemClient::parse_rssi() {
    const char *p = strstr(_rx_buf, "+CSQ:");
    if (!p) return;
    int csq = atoi(p + 5);
    if (csq >= 0 && csq < 99) _rssi = (int8_t)(-113 + 2 * csq);
}

// ---------------------------------------------------------------------------
// Init: power on, basic setup
// ---------------------------------------------------------------------------

void ModemClient::do_init() {
    char cmd[64];

    switch (_init_step) {
        case 0: {
            // Probe first: a PWRKEY pulse on a running modem would switch it off.
            Rx r = exec("AT", "OK", 1000);
            if (r == Rx::OK) {
                _tx_in_flight = false;
                _init_step = 5;
            } else if (r == Rx::FAIL) {
                _tx_in_flight = false;
                digitalWrite(SIM7080_PWRKEY_PIN, HIGH);
                _step_ms = millis();
                _init_step = 1;
            }
            break;
        }
        case 1:
            if (millis() - _step_ms >= 1000) {
                digitalWrite(SIM7080_PWRKEY_PIN, LOW);
                _step_ms = millis();
                _init_step = 2;
            }
            break;
        case 2:
            if (millis() - _step_ms >= 3000) {
                _init_step = 3;
                _retries = 0;
            }
            break;
        case 3: {
            Rx r = exec("AT", "OK", 1000);
            if (r == Rx::OK) {
                _init_step = 5;
            } else if (r == Rx::FAIL && ++_retries >= 10) {
                fail("modem power-on");
            }
            break;
        }
        case 5:  run_step(_init_step, "ATE0", "OK", 1000); break;
        case 6:  run_step(_init_step, "AT+CPIN?", "READY", 3000); break;
        case 7:  run_step(_init_step, "AT+CFUN=1", "OK", 10000); break;
        case 8:  run_step(_init_step, "AT+CNMP=38", "OK", 3000); break;
        case 9:
            snprintf(cmd, sizeof(cmd), "AT+CMNB=%d", MODEM_NETWORK_MODE);
            run_step(_init_step, cmd, "OK", 3000);
            break;
        case 10:
            snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", MODEM_APN);
            run_step(_init_step, cmd, "OK", 3000);
            break;
        case 11: run_step(_init_step, "AT+CMEE=2", "OK", 1000, true); break;
        default:
            _state = State::WAITING_NETWORK;
            _net_step = 0;
            _net_start_ms = millis();
            Serial.println("[MODEM] Modem ready; registering on network");
            break;
    }
}

// ---------------------------------------------------------------------------
// Network registration, PDP context, CA certificate
// ---------------------------------------------------------------------------

void ModemClient::do_connect() {
    char cmd[96];

    switch (_net_step) {
        case 0: {
            if (millis() - _net_start_ms > NET_REG_TIMEOUT_MS) {
                fail("network registration");
                break;
            }
            Rx r = exec("AT+CEREG?", "OK", 2000);
            if (r == Rx::PENDING) break;
            int stat = -1;
            const char *p = strstr(_rx_buf, "+CEREG:");
            if (r == Rx::OK && p) {
                int n = 0;
                if (sscanf(p, "+CEREG: %*d,%d", &n) == 1) stat = n;
            }
            if (stat == 1 || stat == 5) {
                Serial.println("[MODEM] Registered on network");
                _net_step = 1;
            } else {
                _step_ms = millis();
                _net_step = 20;
            }
            break;
        }
        case 20:
            if (millis() - _step_ms >= 2000) _net_step = 0;
            break;
        case 1:
            if (run_step(_net_step, "AT+CSQ", "OK", 2000, true)) {
                parse_rssi();
                Serial.printf("[MODEM] RSSI=%d dBm\n", _rssi);
            }
            break;
        case 2:
            snprintf(cmd, sizeof(cmd), "AT+CNCFG=0,1,\"%s\"", MODEM_APN);
            run_step(_net_step, cmd, "OK", 3000);
            break;
        case 3: {
            Rx r = exec("AT+CNACT?", "OK", 3000);
            if (r == Rx::PENDING) break;
            _net_step = (r == Rx::OK && strstr(_rx_buf, "+CNACT: 0,1")) ? 6 : 4;
            break;
        }
        case 4:
            // PDP activation is confirmed by the "+APP PDP: 0,ACTIVE" URC.
            if (run_step(_net_step, "AT+CNACT=0,1", "0,ACTIVE", 20000)) {
                Serial.println("[MODEM] PDP context active");
                _net_step = 6;
            }
            break;
        case 6:
#if MQTT_USE_TLS
            _net_step = 7;
#else
            _net_step = 30;
#endif
            break;
#if MQTT_USE_TLS
        case 7:
            run_step(_net_step, "AT+CFSINIT", "OK", 3000, true);
            break;
        case 8: {
            snprintf(cmd, sizeof(cmd), "AT+CFSWFILE=3,\"ca.crt\",0,%u,10000",
                     (unsigned)strlen(MQTT_CA_CERT));
            if (run_step(_net_step, cmd, "DOWNLOAD", 5000)) {
                // The modem now takes the raw certificate bytes, then answers OK.
                _rx_len = 0;
                _rx_buf[0] = '\0';
                Serial1.write((const uint8_t *)MQTT_CA_CERT, strlen(MQTT_CA_CERT));
                _cmd_sent_ms = millis();
                _tx_in_flight = true;
            }
            break;
        }
        case 9:
            run_step(_net_step, nullptr, "OK", 15000);
            break;
        case 10:
            run_step(_net_step, "AT+CFSTERM", "OK", 3000, true);
            break;
        case 11:
            run_step(_net_step, "AT+CSSLCFG=\"convert\",2,\"ca.crt\"", "OK", 5000);
            break;
        case 12:
            _net_step = 30;
            break;
#endif
        default:
            _state = State::CONNECTING_MQTT;
            _mqtt_step = 0;
            break;
    }
}

// ---------------------------------------------------------------------------
// MQTT session
// ---------------------------------------------------------------------------

void ModemClient::do_mqtt() {
    char cmd[128];
    uint32_t timeout = 3000;
    bool lenient = false;

    switch (_mqtt_step) {
        case 0:
            snprintf(cmd, sizeof(cmd), "AT+SMDISC");
            lenient = true;
            break;
        case 1:
            snprintf(cmd, sizeof(cmd), "AT+SMCONF=\"URL\",\"%s\",\"%d\"", MQTT_HOST, MQTT_PORT);
            break;
        case 2:
            snprintf(cmd, sizeof(cmd), "AT+SMCONF=\"KEEPTIME\",%d", MQTT_KEEPALIVE_S);
            break;
        case 3:
            snprintf(cmd, sizeof(cmd), "AT+SMCONF=\"CLEANSS\",1");
            break;
        case 4:
            snprintf(cmd, sizeof(cmd), "AT+SMCONF=\"CLIENTID\",\"%s\"", MQTT_CLIENT_ID);
            break;
        case 5:
            snprintf(cmd, sizeof(cmd), "AT+SMCONF=\"USERNAME\",\"%s\"", MQTT_USERNAME);
            break;
        case 6:
            snprintf(cmd, sizeof(cmd), "AT+SMCONF=\"PASSWORD\",\"%s\"", MQTT_PASSWORD);
            break;
        case 7:
#if MQTT_USE_TLS
            snprintf(cmd, sizeof(cmd), "AT+CSSLCFG=\"sslversion\",0,3");
            lenient = true;
            break;
#else
            _mqtt_step++;
            return;
#endif
        case 8:
#if MQTT_USE_TLS
            // Server-auth only: the client certificate argument is an empty string.
            snprintf(cmd, sizeof(cmd), "AT+SMSSL=1,\"ca.crt\",\"\"");
            break;
#else
            _mqtt_step++;
            return;
#endif
        case 9:
            snprintf(cmd, sizeof(cmd), "AT+SMCONN");
            timeout = 40000;
            break;
        default:
            _state = State::READY;
            _ready_step = 0;
            _last_check_ms = millis();
            Serial.println("[MODEM] LTE MQTT ready");
            return;
    }

    run_step(_mqtt_step, cmd, "OK", timeout, lenient);
}

// ---------------------------------------------------------------------------
// Connected: publish queue + periodic link check
// ---------------------------------------------------------------------------

void ModemClient::do_ready() {
    char cmd[128];

    switch (_ready_step) {
        case 0:
            if (_q_count > 0) {
                _ready_step = 3;
            } else if (millis() - _last_check_ms >= CHECK_INTERVAL_MS) {
                _ready_step = 1;
            }
            break;
        case 1:
            if (run_step(_ready_step, "AT+CSQ", "OK", 2000, true)) parse_rssi();
            break;
        case 2: {
            Rx r = exec("AT+SMSTATE?", "OK", 2000);
            if (r == Rx::PENDING) break;
            _last_check_ms = millis();
            _ready_step = 0;
            if (r != Rx::OK || !strstr(_rx_buf, "+SMSTATE: 1")) {
                Serial.println("[MODEM] MQTT session lost; reconnecting");
                _state = State::CONNECTING_MQTT;
                _mqtt_step = 0;
            }
            break;
        }
        case 3: {
            Pending &p = _queue[_q_head];
            snprintf(cmd, sizeof(cmd), "AT+SMPUB=\"%s\",%u,0,%d", p.topic, (unsigned)p.len,
                     p.retain ? 1 : 0);
            Rx r = exec(cmd, ">", 5000);
            if (r == Rx::OK) {
                _rx_len = 0;
                _rx_buf[0] = '\0';
                Serial1.write((const uint8_t *)p.payload, p.len);
                _cmd_sent_ms = millis();
                _tx_in_flight = true;
                _ready_step = 4;
            } else if (r == Rx::FAIL) {
                _ready_step = 0;
                _q_count = 0;
                Serial.println("[MODEM] Publish failed; reconnecting");
                _state = State::CONNECTING_MQTT;
                _mqtt_step = 0;
            }
            break;
        }
        case 4: {
            Rx r = exec(nullptr, "OK", 10000);
            if (r == Rx::PENDING) break;
            _q_head = (_q_head + 1) % QUEUE_SIZE;
            if (_q_count > 0) _q_count--;
            _ready_step = 0;
            if (r == Rx::FAIL) {
                Serial.println("[MODEM] Publish failed; reconnecting");
                _q_count = 0;
                _state = State::CONNECTING_MQTT;
                _mqtt_step = 0;
            }
            break;
        }
        default:
            _ready_step = 0;
            break;
    }
}

bool ModemClient::enqueue(const char *topic, const char *payload, size_t len, bool retain) {
    if (_state != State::READY || _q_count >= QUEUE_SIZE || len == 0 || len >= PAYLOAD_SIZE) {
        return false;
    }
    Pending &p = _queue[(_q_head + _q_count) % QUEUE_SIZE];
    p.topic = topic;
    memcpy(p.payload, payload, len);
    p.len = (uint16_t)len;
    p.retain = retain;
    _q_count++;
    return true;
}

bool ModemClient::publish_telemetry(const BmsData &bms) {
    JsonDocument doc;
    doc["ts"] = millis();
    doc["soc_bms"] = bms.soc_bms;
    doc["soc_display"] = bms.soc_display;
    doc["pack_v"] = bms.pack_v;
    doc["pack_a"] = bms.pack_a;
    doc["aux_v"] = bms.aux_v;
    doc["charging"] = bms.charging;

    char payload[PAYLOAD_SIZE];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n == 0) return false;

    return enqueue(MQTT_TOPIC_TELEMETRY, payload, n, false);
}

bool ModemClient::publish_power(const PowerData &power) {
    JsonDocument doc;
    doc["ts"] = millis();
    doc["car_12v"] = power.car_12v;
    doc["board_v"] = power.board_v;

    char payload[PAYLOAD_SIZE];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n == 0) return false;

    return enqueue(MQTT_TOPIC_STATUS, payload, n, false);
}

bool ModemClient::publish_status(const SystemStatus &status) {
    JsonDocument doc;
    doc["ts"] = millis();
    doc["ble_connected"] = status.ble_connected;
    doc["obd_ready"] = status.obd_ready;
    doc["mqtt_online"] = status.mqtt_online;
    doc["rssi"] = _rssi != 0 ? _rssi : status.cell_rssi;
    doc["uptime_s"] = status.uptime_s;

    char payload[PAYLOAD_SIZE];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n == 0) return false;

    return enqueue(MQTT_TOPIC_STATUS, payload, n, true);
}
