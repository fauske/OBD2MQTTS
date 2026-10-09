#include "../../include/modules/ble_transport.h"
#include <string.h>

// ============================================================================
// NimBLE Callback Shims (must be global/static to work with C-style callbacks)
// ============================================================================

static BleTransport *g_ble_instance = nullptr;

static void nimble_scan_callback(NimBLEScanResults results) {
    (void)results;
}

static void nimble_advertise_callback(NimBLEAdvertisedDevice *dev) {
    if (g_ble_instance && dev->getName().find("vLinker") != std::string::npos) {
        g_ble_instance->onDeviceFound(dev);
    }
}

class ClientCallback : public NimBLEClientCallbacks {
public:
    void onConnect(NimBLEClient *pClient) override {
        if (g_ble_instance) {
            g_ble_instance->onClientConnect();
        }
    }

    void onDisconnect(NimBLEClient *pClient) override {
        if (g_ble_instance) {
            g_ble_instance->onClientDisconnect();
        }
    }
};

static ClientCallback g_client_cb;

class NotifyCallback : public NimBLECharacteristicCallbacks {
public:
    void onNotify(NimBLERemoteCharacteristic *pChar) override {
        std::string data = pChar->getValue();
        if (g_ble_instance && data.length() > 0) {
            g_ble_instance->onNotify((uint8_t *)data.data(), data.length());
        }
    }
};

static NotifyCallback g_notify_cb;

// ============================================================================
// BleTransport Implementation
// ============================================================================

BleTransport::BleTransport() {
    g_ble_instance = this;
}

BleTransport::~BleTransport() {
    if (_client) {
        delete _client;
    }
}

void BleTransport::begin() {
    Serial.println("[BLE] Initializing NimBLE...");

    NimBLEDevice::init("ESP32-OBD2");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);

    NimBLEScan *pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(&g_client_cb, false);
    pScan->setInterval(100);
    pScan->setWindow(99);
    pScan->setActiveScan(true);

    _state = State::SCANNING;
    _scan_started_ms = millis();
    pScan->start(0, nimble_scan_callback, false);

    Serial.println("[BLE] Scanning for ELM327 adapter...");
}

void BleTransport::update() {
    switch (_state) {
        case State::SCANNING:
            do_scan();
            break;
        case State::CONNECTING:
            do_connect();
            break;
        case State::DISCOVERING:
            do_discover();
            break;
        case State::READY:
            // Maintain connection, handle notifies
            break;
        default:
            break;
    }
}

void BleTransport::do_scan() {
    // Scan timeout
    if (millis() - _scan_started_ms > 5000) {
        NimBLEDevice::getScan()->stop();
        Serial.println("[BLE] Scan timeout, restarting...");
        _scan_started_ms = millis();
        NimBLEDevice::getScan()->start(0, nimble_scan_callback, false);
    }

    if (_found_flag) {
        _found_flag = false;
        if (_target) {
            _state = State::CONNECTING;
            _connect_started_ms = millis();
            _connect_attempts = 0;
            Serial.printf("[BLE] Found device: %s, connecting...\n", _target->getName().c_str());
        }
    }
}

void BleTransport::do_connect() {
    if (millis() - _connect_started_ms > 10000) {
        Serial.println("[BLE] Connection timeout");
        _state = State::SCANNING;
        _scan_started_ms = millis();
        _connect_attempts = 0;
        return;
    }

    if (_client == nullptr && _target != nullptr) {
        _client = NimBLEDevice::createClient();
        _client->setClientCallbacks(&g_client_cb, false);
        _client->setConnectionParams(12, 12, 0, 51);

        if (_client->connect(_target, false)) {
            Serial.println("[BLE] Connected to device");
            _state = State::DISCOVERING;
        } else {
            _connect_attempts++;
            if (_connect_attempts > 3) {
                Serial.println("[BLE] Connect failed after retries");
                delete _client;
                _client = nullptr;
                _state = State::SCANNING;
                _scan_started_ms = millis();
            }
        }
    }
}

void BleTransport::do_discover() {
    if (_client && _client->isConnected()) {
        if (discover_characteristics()) {
            _state = State::READY;
            Serial.println("[BLE] Ready for OBD commands");
        } else {
            Serial.println("[BLE] Service discovery failed");
            _client->disconnect();
            _state = State::SCANNING;
            _scan_started_ms = millis();
        }
    }
}

bool BleTransport::discover_characteristics() {
    if (!_client || !_client->isConnected()) {
        return false;
    }

    try {
        NimBLERemoteService *pSvc = _client->getService("18F0");
        if (!pSvc) {
            Serial.println("[BLE] Service 18F0 not found");
            return false;
        }

        _write_char = pSvc->getCharacteristic("2AF1");
        _notify_char = pSvc->getCharacteristic("2AF0");

        if (!_write_char || !_notify_char) {
            Serial.println("[BLE] Characteristics not found");
            return false;
        }

        if (_notify_char->canNotify()) {
            _notify_char->registerForNotify(&g_notify_cb, false);
        }

        return true;
    } catch (const std::exception &e) {
        Serial.printf("[BLE] Discovery exception: %s\n", e.what());
        return false;
    }
}

bool BleTransport::send_command(const char *cmd) {
    if (_cmd_in_flight || _state != State::READY || !_write_char) {
        return false;
    }

    if (!_client || !_client->isConnected()) {
        return false;
    }

    try {
        std::string cmdbuf = cmd;
        cmdbuf += "\r\n";
        _write_char->writeValue((uint8_t *)cmdbuf.c_str(), cmdbuf.length());
        _cmd_in_flight = true;
        _cmd_sent_ms = millis();
        _response_complete = false;
        memset(_response, 0, sizeof(_response));
        _rx_len = 0;
        Serial.printf("[BLE] TX: %s\n", cmd);
        return true;
    } catch (const std::exception &e) {
        Serial.printf("[BLE] Write exception: %s\n", e.what());
        return false;
    }
}

const char *BleTransport::get_response() {
    return _response;
}

void BleTransport::onDeviceFound(NimBLEAdvertisedDevice *dev) {
    _target = dev;
    _found_flag = true;
}

void BleTransport::onClientConnect() {
    Serial.println("[BLE] Client connected");
}

void BleTransport::onClientDisconnect() {
    Serial.println("[BLE] Client disconnected");
    _state = State::DISCONNECTED;
    _cmd_in_flight = false;
    _response_complete = false;
}

void BleTransport::onNotify(uint8_t *data, size_t len) {
    if (len > 0 && _rx_len + len < RX_BUF_SIZE) {
        memcpy(_rx + _rx_len, data, len);
        _rx_len += len;

        // Simple heuristic: response complete when we see "OK" or ">"
        if (strstr(_rx, "OK\r\n") || strstr(_rx, ">") || strstr(_rx, "ERROR")) {
            memcpy(_response, _rx, _rx_len);
            _response[_rx_len] = 0;
            _response_complete = true;
            _cmd_in_flight = false;
            Serial.printf("[BLE] RX: %s", _response);
        }
    }
}
