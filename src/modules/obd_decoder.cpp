#include "../../include/modules/obd_decoder.h"
#include <string.h>
#include <ctype.h>

// ============================================================================
// OBD2 Decoder Implementation
// ============================================================================

ObdDecoder::ObdDecoder(IObdTransport &transport)
    : _transport(transport) {
}

void ObdDecoder::begin() {
    Serial.println("[OBD] Initializing ELM327 decoder...");
    _init_state = InitState::RESET;
    _elm_ready = false;
    _init_started_ms = millis();
}

void ObdDecoder::update() {
    if (!_transport.is_connected()) {
        _elm_ready = false;
        return;
    }

    if (!_elm_ready) {
        do_init();
    } else {
        do_poll();
    }
}

void ObdDecoder::do_init() {
    static const char *init_cmds[] = { "ATZ", "ATE0", "ATL0", "ATSP6" };
    static uint8_t init_idx = 0;
    static bool sent = false;

    // Send init command
    if (!sent) {
        if (!_transport.send_command(init_cmds[init_idx])) {
            return;  // transport busy
        }
        sent = true;
        _cmd_sent_ms = millis();
        return;
    }

    // Wait for response
    if (!_transport.response_ready()) {
        if (millis() - _cmd_sent_ms > 2000) {
            Serial.printf("[OBD] Init timeout on %s\n", init_cmds[init_idx]);
            sent = false;  // retry same step
        }
        return;
    }

    const char *resp = _transport.get_response();
    if (strstr(resp, "OK") || strstr(resp, "ELM327")) {
        Serial.printf("[OBD] %s -> OK\n", init_cmds[init_idx]);
        init_idx++;
        sent = false;

        if (init_idx >= 4) {
            _elm_ready = true;
            _init_state = InitState::READY;
            _poll_state = PollState::IDLE;
            Serial.println("[OBD] ELM327 initialized and ready");
            init_idx = 0;  // reset for next boot
        }
    } else {
        Serial.printf("[OBD] Unexpected response: %s\n", resp);
        sent = false;  // retry
    }
}

void ObdDecoder::do_poll() {
    // Non-blocking polling FSM: 2101 -> 2105 -> wait N seconds -> repeat

    if (_poll_state == PollState::IDLE) {
        if (millis() - _last_poll_ms < 2000) {
            return;  // not yet time
        }
        _poll_state = PollState::REQUESTING_2101;
    }

    if (_poll_state == PollState::REQUESTING_2101) {
        if (_transport.send_command("2101")) {
            _cmd_sent_ms = millis();
            _poll_state = PollState::WAITING_2101;
            Serial.println("[OBD] Sent 2101");
        }
        return;
    }

    if (_poll_state == PollState::WAITING_2101) {
        if (_transport.response_ready()) {
            const char *resp = _transport.get_response();
            Serial.printf("[OBD] 2101 response: %s\n", resp);
            _poll_state = PollState::REQUESTING_2105;
        } else if (millis() - _cmd_sent_ms > 5000) {
            Serial.println("[OBD] 2101 timeout");
            _poll_state = PollState::IDLE;
            _last_poll_ms = millis();
        }
        return;
    }

    if (_poll_state == PollState::REQUESTING_2105) {
        if (_transport.send_command("2105")) {
            _cmd_sent_ms = millis();
            _poll_state = PollState::WAITING_2105;
            Serial.println("[OBD] Sent 2105");
        }
        return;
    }

    if (_poll_state == PollState::WAITING_2105) {
        if (_transport.response_ready()) {
            const char *resp = _transport.get_response();
            Serial.printf("[OBD] 2105 response: %s\n", resp);
            process_response();
            _poll_state = PollState::DONE;
        } else if (millis() - _cmd_sent_ms > 5000) {
            Serial.println("[OBD] 2105 timeout");
            _poll_state = PollState::IDLE;
            _last_poll_ms = millis();
        }
        return;
    }

    if (_poll_state == PollState::DONE) {
        _poll_state = PollState::IDLE;
        _last_poll_ms = millis();
        _bms_data.valid = true;
        _bms_data.last_update_ms = millis();
        Serial.println("[OBD] Poll cycle complete");
    }
}

bool ObdDecoder::have_fresh_data() const {
    if (!_bms_data.valid) return false;
    uint32_t age = millis() - _bms_data.last_update_ms;
    return age < 15000;
}

void ObdDecoder::request_trip_queries() {
    Serial.println("[OBD] Requesting odometer and DTC queries");
}

bool ObdDecoder::parse_hex_response(const char *response, uint8_t *out, size_t &out_len) {
    out_len = 0;
    if (!response) return false;

    const char *p = response;
    while (*p && out_len < 256) {
        // Skip whitespace
        while (*p && isspace(*p)) p++;
        if (!*p) break;

        // Parse two hex digits
        uint8_t byte = 0;
        for (int i = 0; i < 2; i++) {
            if (isxdigit(*p)) {
                byte = byte * 16 + (isdigit(*p) ? (*p - '0') : (tolower(*p) - 'a' + 10));
                p++;
            } else {
                return false;
            }
        }
        out[out_len++] = byte;
    }
    return out_len > 0;
}

bool ObdDecoder::decode_2101(const uint8_t *data, size_t len) {
    (void)data;
    (void)len;
    return false;
}

bool ObdDecoder::decode_2105(const uint8_t *data, size_t len) {
    (void)data;
    (void)len;
    return false;
}

float ObdDecoder::decode_voltage(uint16_t raw) {
    return (raw * 0.0625f);
}

float ObdDecoder::decode_current(int16_t raw) {
    return (raw * 0.1f);
}

float ObdDecoder::decode_soc(uint8_t raw) {
    return (raw * 0.5f);
}

void ObdDecoder::process_response() {
    // Reserved for when we have real response data to parse
}
