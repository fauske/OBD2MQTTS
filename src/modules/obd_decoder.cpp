#include "../../include/modules/obd_decoder.h"
#include <string.h>

// ============================================================================
// OBD2 Decoder Implementation with Ioniq BMS Support
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

    if (!sent) {
        if (!_transport.send_command(init_cmds[init_idx])) {
            return;
        }
        sent = true;
        _cmd_sent_ms = millis();
        return;
    }

    if (!_transport.response_ready()) {
        if (millis() - _cmd_sent_ms > 2000) {
            Serial.printf("[OBD] Init timeout on %s\n", init_cmds[init_idx]);
            sent = false;
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
            init_idx = 0;
        }
    } else {
        Serial.printf("[OBD] Unexpected response: %s\n", resp);
        sent = false;
    }
}

void ObdDecoder::do_poll() {
    if (_poll_state == PollState::IDLE) {
        if (millis() - _last_poll_ms < 2000) {
            return;
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
            process_2101_response();
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
            process_2105_response();
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

void ObdDecoder::process_2101_response() {
    const char *resp = _transport.get_response();
    Serial.printf("[OBD] 2101 raw: %s\n", resp);

    if (!IoniqBmsDecoder::parse_hex_response(resp, _response_bin, _response_bin_len)) {
        Serial.println("[OBD] 2101 parse failed");
        return;
    }

    // Response format: "62 01 <data>" where 62=response, 01=PID
    // Skip first 2 bytes (62 01) and decode the rest
    if (_response_bin_len < 16) {
        Serial.printf("[OBD] 2101 data too short: %u bytes\n", _response_bin_len);
        return;
    }

    // Payload starts at index 2
    uint8_t *payload = _response_bin + 2;
    size_t payload_len = _response_bin_len - 2;

    IoniqBmsDecoder::decode_2101(
        payload, payload_len,
        _bms_data.soc_bms,
        _bms_data.soc_display,
        _bms_data.pack_v,
        _bms_data.pack_a,
        _bms_data.cell_max_no,
        _bms_data.cell_min_no,
        _bms_data.cell_v_max,
        _bms_data.cell_v_min
    );
}

void ObdDecoder::process_2105_response() {
    const char *resp = _transport.get_response();
    Serial.printf("[OBD] 2105 raw: %s\n", resp);

    if (!IoniqBmsDecoder::parse_hex_response(resp, _response_bin, _response_bin_len)) {
        Serial.println("[OBD] 2105 parse failed");
        return;
    }

    if (_response_bin_len < 10) {
        Serial.printf("[OBD] 2105 data too short: %u bytes\n", _response_bin_len);
        return;
    }

    uint8_t *payload = _response_bin + 2;
    size_t payload_len = _response_bin_len - 2;

    IoniqBmsDecoder::decode_2105(
        payload, payload_len,
        _bms_data.temp_max,
        _bms_data.temp_min,
        _bms_data.temp_inlet,
        _bms_data.soh,
        _bms_data.avail_chg_kw,
        _bms_data.avail_dis_kw
    );
}

bool ObdDecoder::have_fresh_data() const {
    if (!_bms_data.valid) return false;
    uint32_t age = millis() - _bms_data.last_update_ms;
    return age < 15000;
}

void ObdDecoder::request_trip_queries() {
    Serial.println("[OBD] Requesting odometer and DTC queries");
}
