#include "../../include/modules/obd_decoder.h"

// ============================================================================
// OBD2 Decoder Implementation
// ============================================================================

ObdDecoder::ObdDecoder(IObdTransport &transport)
    : _transport(transport) {
}

void ObdDecoder::begin() {
    Serial.println("[OBD] Initializing ELM327 decoder...");
    _init_state = InitState::RESET;
}

void ObdDecoder::update() {
    if (!_transport.is_connected()) {
        _elm_ready = false;
        return;
    }

    // ELM327 initialization sequence
    if (!_elm_ready) {
        do_init();
        return;
    }

    // Poll BMS data on interval
    do_poll();
}

void ObdDecoder::do_init() {
    // TODO: Send AT commands to initialize ELM327
    // Sequence: ATZ (reset) -> ATE0 (echo off) -> ATL0 (linefeeds off) -> ATSP6 (protocol)
}

void ObdDecoder::do_poll() {
    // TODO: Send 2101 and 2105 commands on POLL_INTERVAL_MS
}

bool ObdDecoder::have_fresh_data() const {
    if (!_bms_data.valid) return false;
    uint32_t age = millis() - _bms_data.last_update_ms;
    return age < 15000;  // 15 second staleness threshold
}

void ObdDecoder::request_trip_queries() {
    // TODO: Swap CAN header and request odometer + DTCs
}

bool ObdDecoder::decode_2101(const uint8_t *data, size_t len) {
    // TODO: Parse 2101 response and populate BmsData
    return false;
}

bool ObdDecoder::decode_2105(const uint8_t *data, size_t len) {
    // TODO: Parse 2105 response and populate BmsData
    return false;
}

bool ObdDecoder::parse_hex_response(const char *response, uint8_t *out, size_t &out_len) {
    // TODO: Convert "61 01 AA BB CC..." hex string to binary
    return false;
}
