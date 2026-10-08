#include "../../include/modules/ble_transport.h"

// ============================================================================
// BLE Transport Implementation (NimBLE placeholder)
// ============================================================================

BleTransport::BleTransport() {
}

void BleTransport::begin() {
    Serial.println("[BLE] Initializing BLE transport...");
    // TODO: Initialize NimBLE, start scanning for ELM327 adapter
    _state = State::SCANNING;
}

void BleTransport::update() {
    switch (_state) {
        case State::SCANNING:
            // TODO: Check for discovered devices, handle connection
            break;
        case State::CONNECTING:
            // TODO: Handle connection timeout
            break;
        case State::DISCOVERING:
            // TODO: Service discovery
            break;
        case State::READY:
            // TODO: Handle notify callbacks, command responses
            break;
        default:
            break;
    }
}

bool BleTransport::send_command(const char *cmd) {
    if (_cmd_in_flight || _state != State::READY) {
        return false;
    }
    // TODO: Send command via BLE write characteristic
    _cmd_in_flight = true;
    _cmd_sent_ms = millis();
    return true;
}

const char *BleTransport::get_response() {
    return _response;
}
