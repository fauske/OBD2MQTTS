#pragma once

// ============================================================================
// Abstract interface for OBD transport (decouples protocol from decoder)
// ============================================================================
// Implementations: BLE, WiFi, serial, USB, etc.
// The OBD decoder uses ONLY this interface.

class IObdTransport {
public:
    virtual ~IObdTransport() = default;

    // Lifecycle
    virtual void begin() = 0;                    // initialize transport
    virtual void update() = 0;                   // pump state machine
    virtual bool is_connected() const = 0;       // link status

    // Commands
    virtual bool send_command(const char *cmd) = 0;  // queue command
    virtual bool is_busy() const = 0;                 // command in flight?
    virtual bool response_ready() const = 0;         // response complete?
    virtual const char *get_response() = 0;          // read response buffer
};
