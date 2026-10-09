#pragma once

#include "../i_obd_transport.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

// ============================================================================
// BLE transport for ELM327 OBD adapters (NimBLE-based)
// ============================================================================
// Connects to Vgate vLinker MC+ or similar BLE ELM327 adapters.
// Non-blocking state machine: IDLE -> SCANNING -> FOUND -> CONNECTING ->
// DISCOVERING -> READY

class BleTransport : public IObdTransport {
public:
    enum class State : uint8_t {
        IDLE,
        SCANNING,
        FOUND,
        CONNECTING,
        DISCOVERING,
        READY,
        DISCONNECTED
    };

    BleTransport();
    virtual ~BleTransport();

    void  begin() override;
    void  update() override;
    bool  is_connected() const override { return _state == State::READY; }
    bool  is_busy() const override      { return _cmd_in_flight; }
    bool  send_command(const char *cmd) override;
    bool  response_ready() const override { return _response_complete; }
    const char *get_response() override;

    State state() const { return _state; }

    // Callbacks from NimBLE (must be public for shim functions)
    void onDeviceFound(NimBLEAdvertisedDevice *dev);
    void onClientConnect();
    void onClientDisconnect();
    void onNotify(uint8_t *data, size_t len);

private:
    State _state = State::IDLE;

    // NimBLE objects
    NimBLEClient *_client = nullptr;
    NimBLERemoteCharacteristic *_write_char = nullptr;
    NimBLERemoteCharacteristic *_notify_char = nullptr;
    NimBLEAdvertisedDevice *_target = nullptr;

    // State flags
    bool     _cmd_in_flight      = false;
    volatile bool _response_complete = false;
    volatile bool _found_flag    = false;
    volatile bool _disconnect_flag = false;

    uint32_t _cmd_sent_ms        = 0;
    uint32_t _scan_started_ms    = 0;
    uint32_t _connect_started_ms = 0;
    uint8_t  _connect_attempts   = 0;

    // RX buffer and response
    static constexpr size_t RX_BUF_SIZE = 512;
    char _rx[RX_BUF_SIZE] = {};
    volatile size_t _rx_len = 0;
    char _response[RX_BUF_SIZE] = {};

    // Methods
    void do_scan();
    void do_connect();
    void do_discover();
    bool discover_characteristics();
};
