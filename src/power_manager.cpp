#include "power_manager.h"
#include "config.h"
#include <esp_sleep.h>

PowerManager::PowerManager() {
}

void PowerManager::begin() {
    Serial.println("[PWR] Initializing power manager...");

    pinMode(ADC_12V_PIN, INPUT);
    analogSetPinAttenuation(ADC_12V_PIN, ADC_11db);

    read_voltages();
    _last_adc_read_ms = millis();

    if (ignition_on()) {
        _mode = Mode::ACTIVE;
        _ignition_was_on = true;
        _ignition_on_time_ms = _last_adc_read_ms;
        Serial.printf("[PWR] Initial mode -> ACTIVE (12V=%.1f)\n", _car_12v);
    } else if (_car_12v > CHARGING_DETECT_THRESHOLD_V) {
        _mode = Mode::AWAKE;
        _ignition_off_time_ms = _last_adc_read_ms;
        Serial.printf("[PWR] Initial mode -> AWAKE (charging, 12V=%.1f)\n", _car_12v);
    } else {
        _mode = Mode::MINIMAL;
        _ignition_off_time_ms = _last_adc_read_ms;
        Serial.printf("[PWR] Initial mode -> MINIMAL (parked, 12V=%.1f)\n", _car_12v);
    }
}

void PowerManager::update() {
    if (millis() - _last_adc_read_ms > 30000) {
        _last_adc_read_ms = millis();
        read_voltages();
        update_mode();
    }
}

bool PowerManager::ignition_on() const {
    return _car_12v > IGNITION_ON_THRESHOLD_V;
}

void PowerManager::read_voltages() {
    constexpr uint8_t SAMPLE_COUNT = 8;
    uint32_t adc_millivolts_total = 0;
    for (uint8_t sample = 0; sample < SAMPLE_COUNT; ++sample) {
        adc_millivolts_total += analogReadMilliVolts(ADC_12V_PIN);
    }

    const float adc_voltage =
        (adc_millivolts_total / static_cast<float>(SAMPLE_COUNT)) / 1000.0f;
    _car_12v = adc_voltage * ADC_12V_DIVIDER;
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
            if (_car_12v > CHARGING_DETECT_THRESHOLD_V) {
                _mode = Mode::AWAKE;
                Serial.printf("[PWR] Mode -> AWAKE (charging, 12V=%.1f)\n", _car_12v);
            } else {
                _mode = Mode::MINIMAL;
                Serial.printf("[PWR] Mode -> MINIMAL (parked, 12V=%.1f)\n", _car_12v);
            }
        }
    } else if (_mode == Mode::AWAKE && _car_12v < CHARGING_RELEASE_THRESHOLD_V) {
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
