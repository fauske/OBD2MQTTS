#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <XPowers.h>

// ============================================================================
// AXP2101 Power Manager
// ============================================================================
// Manages the AXP2101 power management IC on the LilyGo T-SIM7080G-S3.
// Handles battery monitoring, charging control, and power rail management.

class Axp2101Manager {
public:
    Axp2101Manager();

    bool begin();
    void update();
    bool is_initialized() const { return _initialized; }

    // Voltage readings
    float get_vbus_voltage() const;
    float get_battery_voltage() const;
    float get_vsys_voltage() const;
    float get_board_voltage() const { return get_vsys_voltage(); }

    // Battery state
    int get_battery_percent() const;
    bool is_charging() const;
    bool is_discharging() const;
    bool is_connected() const;  // VBUS connected

    // Power control
    void enable_modem_power(bool enable);
    void enable_esp_power(bool enable);
    void shutdown();

private:
    XPowers _pmu;
    bool _initialized = false;
    uint32_t _last_update_ms = 0;

    // Cached values
    float _vbus_v = 0.0f;
    float _batt_v = 0.0f;
    float _vsys_v = 0.0f;
    int _batt_percent = 0;
    bool _is_charging = false;

    void init_power_rails();
    void read_voltages();
};
