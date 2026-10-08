#pragma once

#include "../i_obd_transport.h"
#include <Arduino.h>

// ============================================================================
// BLE transport for ELM327 OBD adapters (NimBLE-based)
// ============================================================================
// Connects to Vgate vLinker MC+ or similar BLE ELM327 adapters.
// Implements the IObdTransport interface.

class BleTransport : public IObdTransport {
public:
    enum class State : uint8_t {
        IDLE,
        SCANNING,
        FOUND,           // advertised device captured, ready to connect
        CONNECTING,
        DISCOVERING,     // resolving services + characteristics
        READY,           // connected, characteristics subscribed
        DISCONNECTED     // transient; goes back to SCANNING
    };

    BleTransport();

    void  begin() override;
    void  update() override;
    bool  is_connected() const override { return _state == State::READY; }
    bool  is_busy() const override      { return _cmd_in_flight; }
    bool  send_command(const char *cmd) override;
    bool  response_ready() const override { return _response_complete; }
    const char *get_response() override;

    State state() const { return _state; }

private:
    State _state = State::IDLE;

    // TODO: NimBLE pointers and state will be added here
    // For now, this is a placeholder

    bool     _cmd_in_flight      = false;
    volatile bool _response_complete = false;
    uint32_t _cmd_sent_ms        = 0;

    static constexpr size_t RX_BUF_SIZE = 512;
    char     _rx[RX_BUF_SIZE]    = {};
    char     _response[RX_BUF_SIZE] = {};
};
