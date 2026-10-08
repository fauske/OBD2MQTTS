#include "../../include/modules/ble_transport.h"

// ============================================================================
// BLE Transport Implementation (non-blocking state machine placeholder)
// ============================================================================

BleTransport::BleTransport() {
}

void BleTransport::begin() {
    Serial.println("[BLE] Initializing BLE transport...");
    _state = State::SCANNING;
    _scan_started_ms = millis();
    _simulated_ready = false;
    _response_complete = false;
}

void BleTransport::update() {
    switch (_state) {
        case State::SCANNING:
            if (millis() - _scan_started_ms > 1500) {
                // This is a realistic non-blocking placeholder: the transport reports
                // a device as ready once the scan window has elapsed.
                _state = State::READY;
                _simulated_ready = true;
                _response_complete = false;
                Serial.println("[BLE] Device found and ready for OBD commands");
            }
            break;

        case State::READY:
            _response_complete = false;
            break;

        default:
            break;
    }
}

bool BleTransport::send_command(const char *cmd) {
    if (_cmd_in_flight || _state != State::READY) {
        return false;
    }

    strncpy(_response, "OK\r\n", sizeof(_response));
    _response_complete = true;
    _cmd_in_flight = true;
    _cmd_sent_ms = millis();

    Serial.printf("[BLE] TX: %s\n", cmd);
    return true;
}

const char *BleTransport::get_response() {
    return _response;
}
