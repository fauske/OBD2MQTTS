#pragma once

#include <Arduino.h>

// ============================================================================
// Power Manager for ignition-based state and sleep management
// ============================================================================
// Monitors ignition voltage, manages transitions between ACTIVE, AWAKE,
// and MINIMAL (deep sleep / parked) states.

class PowerManager {
public:
    enum class Mode : uint8_t {
        ACTIVE,    // Ignition ON: full telemetry and OTA window
        AWAKE,     // Ignition OFF but charging: telemetry continues
        MINIMAL    // Parked: deep-sleep between periodic measurements
    };

    PowerManager();

    void begin();
    void update();
    Mode current_mode() const { return _mode; }

    bool ignition_on() const;
    float get_car_12v() const { return _car_12v; }
    float get_board_v() const { return _board_v; }

    // AXP2101 + modem control (LilyGo T-SIM7080G-S3)
    bool pmu_ok() const { return _pmu_ok; }
    float battery_v() const { return _batt_v; }
    int battery_percent() const { return _batt_pct; }
    void modem_power_on();   // enable PMU rails, pulse PWRKEY
    void modem_power_off();  // AT+CPOWD=1 should be sent first; then drops rails

    bool should_sleep() const;
    void enter_sleep(uint32_t seconds);

private:
    Mode _mode = Mode::MINIMAL;

    float _car_12v = 0.0f;
    float _board_v = 0.0f;
    float _batt_v = 0.0f;
    int _batt_pct = -1;
    bool _pmu_ok = false;
    uint32_t _last_adc_read_ms = 0;
    uint32_t _ignition_on_time_ms = 0;
    uint32_t _ignition_off_time_ms = 0;
    bool _ignition_was_on = false;

    static constexpr float IGNITION_ON_THRESHOLD = 12.0f;
    static constexpr float IGNITION_OFF_THRESHOLD = 11.5f;
    static constexpr uint32_t IGNITION_DEBOUNCE_MS = 5000;
    static constexpr float CHARGE_DETECT_12V = 13.5f;
    static constexpr float CHARGE_RELEASE_12V = 13.0f;

    bool init_pmu();
    void read_voltages();
    void update_mode();
};
