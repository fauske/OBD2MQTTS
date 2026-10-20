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
    if (_trip_query_pending && millis() - _trip_query_started_ms > 90000) {
        _trip_query_pending = false;
        _trip_queries_complete = true;
        _poll_state = PollState::IDLE;
        _last_poll_ms = millis();
        Serial.println("[OBD] Trip query timed out");
    }

    if (!_transport.is_connected()) {
        _elm_ready = false;
        _bms_data.valid = false;
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
        _poll_2101_ok = false;
        _poll_2105_ok = false;
        _bms_data.valid = false;
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
            _poll_2101_ok = process_2101_response();
            _poll_state = PollState::REQUESTING_2105;
        } else if (millis() - _cmd_sent_ms > 5000) {
            Serial.println("[OBD] 2101 timeout");
            _bms_data.valid = false;
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
            _poll_2105_ok = process_2105_response();
            _poll_state = PollState::DONE;
        } else if (millis() - _cmd_sent_ms > 5000) {
            Serial.println("[OBD] 2105 timeout");
            _bms_data.valid = false;
            _poll_state = PollState::IDLE;
            _last_poll_ms = millis();
        }
        return;
    }

    if (_poll_state == PollState::DONE) {
        _last_poll_ms = millis();
        _bms_data.valid = _poll_2101_ok && _poll_2105_ok;
        if (_bms_data.valid) {
            _bms_data.last_update_ms = millis();
        }
        _poll_state = _trip_query_pending
            ? PollState::REQUESTING_ODOMETER
            : PollState::IDLE;
        Serial.println("[OBD] Poll cycle complete");
        return;
    }

    if (_poll_state == PollState::REQUESTING_ODOMETER) {
        if (_transport.send_command("01A6")) {
            _cmd_sent_ms = millis();
            _poll_state = PollState::WAITING_ODOMETER;
            Serial.println("[OBD] Sent 01A6 odometer query");
        }
        return;
    }

    if (_poll_state == PollState::WAITING_ODOMETER) {
        if (_transport.response_ready()) {
            _trip_data.odo_valid = process_odometer_response();
            _poll_state = PollState::REQUESTING_DTC;
        } else if (millis() - _cmd_sent_ms > 5000) {
            _trip_data.odo_valid = false;
            _poll_state = PollState::REQUESTING_DTC;
            Serial.println("[OBD] Odometer query timeout");
        }
        return;
    }

    if (_poll_state == PollState::REQUESTING_DTC) {
        if (_transport.send_command("03")) {
            _cmd_sent_ms = millis();
            _poll_state = PollState::WAITING_DTC;
            Serial.println("[OBD] Sent DTC query");
        }
        return;
    }

    if (_poll_state == PollState::WAITING_DTC) {
        if (_transport.response_ready()) {
            _trip_data.dtc_valid = process_dtc_response();
            _trip_query_pending = false;
            _trip_queries_complete = true;
            _poll_state = PollState::IDLE;
            _last_poll_ms = millis();
        } else if (millis() - _cmd_sent_ms > 5000) {
            _trip_data.dtc_valid = false;
            _trip_query_pending = false;
            _trip_queries_complete = true;
            _poll_state = PollState::IDLE;
            _last_poll_ms = millis();
            Serial.println("[OBD] DTC query timeout");
        }
    }
}

bool ObdDecoder::process_2101_response() {
    const char *resp = _transport.get_response();
    Serial.printf("[OBD] 2101 raw: %s\n", resp);

    if (!IoniqBmsDecoder::parse_hex_response(resp, _response_bin, _response_bin_len)) {
        Serial.println("[OBD] 2101 parse failed");
        return false;
    }

    if (_response_bin_len < 2 || _response_bin[0] != 0x61 || _response_bin[1] != 0x01) {
        Serial.println("[OBD] 2101 response header invalid");
        return false;
    }
    if (_response_bin_len < 16) {
        Serial.printf("[OBD] 2101 data too short: %u bytes\n", _response_bin_len);
        return false;
    }

    // Payload starts at index 2
    uint8_t *payload = _response_bin + 2;
    size_t payload_len = _response_bin_len - 2;

    const bool decoded = IoniqBmsDecoder::decode_2101(
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
    if (decoded) {
        _bms_data.charging = _bms_data.pack_a < -0.5f;
    }
    return decoded;
}

bool ObdDecoder::process_2105_response() {
    const char *resp = _transport.get_response();
    Serial.printf("[OBD] 2105 raw: %s\n", resp);

    if (!IoniqBmsDecoder::parse_hex_response(resp, _response_bin, _response_bin_len)) {
        Serial.println("[OBD] 2105 parse failed");
        return false;
    }

    if (_response_bin_len < 2 || _response_bin[0] != 0x61 || _response_bin[1] != 0x05) {
        Serial.println("[OBD] 2105 response header invalid");
        return false;
    }
    if (_response_bin_len < 10) {
        Serial.printf("[OBD] 2105 data too short: %u bytes\n", _response_bin_len);
        return false;
    }

    uint8_t *payload = _response_bin + 2;
    size_t payload_len = _response_bin_len - 2;

    return IoniqBmsDecoder::decode_2105(
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
    if (_trip_query_pending) return;
    _trip_data = TripData{};
    _trip_query_pending = true;
    _trip_queries_complete = false;
    _trip_query_started_ms = millis();
    Serial.println("[OBD] Queued odometer and DTC queries");
}

bool ObdDecoder::process_odometer_response() {
    if (!IoniqBmsDecoder::parse_hex_response(
            _transport.get_response(), _response_bin, _response_bin_len) ||
        _response_bin_len < 6 || _response_bin[0] != 0x41 ||
        _response_bin[1] != 0xA6) {
        Serial.println("[OBD] Odometer response invalid or unsupported");
        return false;
    }

    const uint32_t raw = (static_cast<uint32_t>(_response_bin[2]) << 24) |
                         (static_cast<uint32_t>(_response_bin[3]) << 16) |
                         (static_cast<uint32_t>(_response_bin[4]) << 8) |
                         static_cast<uint32_t>(_response_bin[5]);
    _trip_data.odometer_km = raw / 10 + ((raw % 10) >= 5 ? 1 : 0);
    return true;
}

bool ObdDecoder::process_dtc_response() {
    if (!IoniqBmsDecoder::parse_hex_response(
            _transport.get_response(), _response_bin, _response_bin_len) ||
        _response_bin_len < 1 || _response_bin[0] != 0x43 ||
        ((_response_bin_len - 1) % 2) != 0) {
        Serial.println("[OBD] DTC response invalid or unsupported");
        return false;
    }

    static const char DTC_FAMILY[] = {'P', 'C', 'B', 'U'};
    _trip_data.dtc_count = 0;
    for (size_t index = 1;
         index + 1 < _response_bin_len &&
         _trip_data.dtc_count < TripData::MAX_DTC;
         index += 2) {
        const uint8_t first = _response_bin[index];
        const uint8_t second = _response_bin[index + 1];
        if (first == 0 && second == 0) continue;

        char *code = _trip_data.dtc[_trip_data.dtc_count++];
        snprintf(code, sizeof(_trip_data.dtc[0]), "%c%X%X%X%X",
                 DTC_FAMILY[(first >> 6) & 0x03], (first >> 4) & 0x03,
                 first & 0x0F, second >> 4, second & 0x0F);
    }
    return true;
}
