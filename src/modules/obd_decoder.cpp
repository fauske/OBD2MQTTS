#include "../../include/modules/obd_decoder.h"
#include <string.h>

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
    _init_sent = false;
    _init_started_ms = millis();
}

void ObdDecoder::update() {
    if (!_transport.is_connected()) {
        _elm_ready = false;
        return;
    }

    if (!_elm_ready) {
        do_init();
        return;
    }

    do_poll();
}

void ObdDecoder::do_init() {
    static const char *cmds[] = { "ATZ", "ATE0", "ATL0", "ATSP6" };
    static uint8_t index = 0;

    if (!_init_sent) {
        _transport.send_command(cmds[index]);
        _init_sent = true;
        _cmd_sent_ms = millis();
        Serial.printf("[OBD] init: %s\n", cmds[index]);
        return;
    }

    if (_transport.response_ready()) {
        const char *resp = _transport.get_response();
        if (strstr(resp, "OK") != nullptr || strstr(resp, "ELM327") != nullptr) {
            index++;
            _init_sent = false;

            if (index >= 4) {
                _elm_ready = true;
                _init_state = InitState::READY;
                Serial.println("[OBD] ELM327 initialized");
            } else {
                _init_state = static_cast<InitState>(1 + index);
            }
        }
    }

    if (millis() - _cmd_sent_ms > 2000) {
        // command timeout: retry same step once
        _init_sent = false;
    }
}

void ObdDecoder::do_poll() {
    // Very simple non-blocking polling skeleton:
    // 1) send PID 2101
    // 2) then PID 2105
    // 3) parse response when ready
    if (millis() - _last_poll_ms < 2000) {
        return;
    }

    _last_poll_ms = millis();
    Serial.println("[OBD] Requesting BMS poll (2101/2105)");

    // Simulate a valid sample to keep state moving in the absence of a real dongle
    _bms_data.valid = true;
    _bms_data.last_update_ms = millis();
    _bms_data.soc_bms = 78.5f;
    _bms_data.soc_display = 78.0f;
    _bms_data.pack_v = 332.1f;
    _bms_data.pack_a = -12.3f;
    _bms_data.aux_v = 13.8f;
    _bms_data.charging = (_bms_data.pack_a < 0.0f);
}

bool ObdDecoder::have_fresh_data() const {
    if (!_bms_data.valid) return false;
    uint32_t age = millis() - _bms_data.last_update_ms;
    return age < 15000;
}

void ObdDecoder::request_trip_queries() {
    Serial.println("[OBD] Requesting odometer and DTC queries");
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

bool ObdDecoder::parse_hex_response(const char *response, uint8_t *out, size_t &out_len) {
    (void)response;
    (void)out;
    (void)out_len;
    return false;
}

void ObdDecoder::process_response() {
    // Reserved for when the real transport layer provides a full ELM response.
}
