#include "../../include/power_manager.h"
#include <esp_sleep.h>

PowerManager::PowerManager() {
}

void PowerManager::begin() {
    Serial.println("[PWR] Initializing power manager...");

    // ADC input for voltage measurement on a suitable GPIO.
    // For now, this is a simulated placeholder.
    pinMode(10, INPUT);

    read_voltages();
    update_mode();
}

void PowerManager::update() {
    if (millis() - _last_adc_read_ms > 30000) {
        _last_adc_read_ms = millis();
        read_voltages();
        update_mode();
    }
}

bool PowerManager::ignition_on() const {
    return _car_12v > IGNITION_ON_THRESHOLD;
}

void PowerManager::read_voltages() {
    // Placeholder: real ADC implementation should be used later.
    // For now, simulated values intentionally keep the logic testable.
    _car_12v = 12.5f;
    _board_v = 4.2f;
}

void PowerManager::update_mode() {
    bool ignition_detected = ignition_on();

    if (ignition_detected && !_ignition_was_on) {
        _ignition_on_time_ms = millis();
        if (millis() - _ignition_off_time_ms > IGNITION_DEBOUNCE_MS) {
            _mode = Mode::ACTIVE;
            _ignition_was_on = true;
            Serial.printf("[PWR] Mode -> ACTIVE (12V=%.1f)\n", _car_12v);
        }
    } else if (!ignition_detected && _ignition_was_on) {
        _ignition_off_time_ms = millis();
        if (millis() - _ignition_on_time_ms > IGNITION_DEBOUNCE_MS) {
            _ignition_was_on = false;
            if (_car_12v > CHARGE_DETECT_12V) {
                _mode = Mode::AWAKE;
                Serial.printf("[PWR] Mode -> AWAKE (charging, 12V=%.1f)\n", _car_12v);
            } else {
                _mode = Mode::MINIMAL;
                Serial.printf("[PWR] Mode -> MINIMAL (parked, 12V=%.1f)\n", _car_12v);
            }
        }
    } else if (_mode == Mode::AWAKE && _car_12v < CHARGE_RELEASE_12V) {
        _mode = Mode::MINIMAL;
        Serial.printf("[PWR] Mode -> MINIMAL (charge ended, 12V=%.1f)\n", _car_12v);
    }
}

bool PowerManager::should_sleep() const {
    return _mode != Mode::ACTIVE;
}

void PowerManager::enter_sleep(uint32_t seconds) {
    Serial.printf("[PWR] Entering deep sleep for %u seconds\n", seconds);
    esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
    esp_deep_sleep_start();
}
