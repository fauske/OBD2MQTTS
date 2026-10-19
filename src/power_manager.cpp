#include "power_manager.h"
#include "config.h"
#include <esp_sleep.h>
#include <driver/rtc_io.h>

PowerManager::PowerManager() {
}

void PowerManager::begin() {
    Serial.println("[PWR] Initializing power manager...");

    // ADC input for voltage measurement on a suitable GPIO.
    // For now, this is a simulated placeholder.
    pinMode(ADC_12V_PIN, INPUT);
    pinMode(IGNITION_PIN, IGNITION_ACTIVE_LEVEL == HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);

    read_voltages();
    // Decide the boot mode directly; the debounce in update_mode() would delay it by seconds.
    _charging_now = _car_12v > CHARGING_DETECT_THRESHOLD_V;
    _mode = target_mode();
    _pending = _mode;
    Serial.printf("[PWR] Boot mode=%d (12V=%.1f)\n", (int)_mode, _car_12v);
}

void PowerManager::update() {
    if (millis() - _last_adc_read_ms > 30000) {
        _last_adc_read_ms = millis();
        read_voltages();
    }
    update_mode();
}

bool PowerManager::ignition_on() const {
    return digitalRead(IGNITION_PIN) == IGNITION_ACTIVE_LEVEL;
}

void PowerManager::read_voltages() {
#if ADC_12V_SIMULATE
    _car_12v = 12.5f;
#else
    uint32_t mv = 0;
    for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(ADC_12V_PIN);
    _car_12v = (mv / 16) / 1000.0f * ADC_12V_DIVIDER;
#endif
    _board_v = 4.2f;  // placeholder; main() uses the AXP2101 battery voltage when available
}

PowerManager::Mode PowerManager::target_mode() const {
    if (ignition_on()) return Mode::ACTIVE;
    return _charging_now ? Mode::AWAKE : Mode::MINIMAL;
}

void PowerManager::update_mode() {
    // Ignition off + 12V raised by the car's DC-DC converter = charging.
    if (_car_12v > CHARGING_DETECT_THRESHOLD_V) _charging_now = true;
    else if (_car_12v < CHARGING_RELEASE_THRESHOLD_V) _charging_now = false;

    Mode target = target_mode();
    if (target == _mode) {
        _debouncing = false;
        return;
    }
    if (!_debouncing || target != _pending) {
        _debouncing = true;
        _pending = target;
        _ignition_on_time_ms = millis();
        return;
    }
    if (millis() - _ignition_on_time_ms < IGNITION_DEBOUNCE_MS) return;

    _debouncing = false;
    _mode = target;
    const char *name = target == Mode::ACTIVE ? "ACTIVE" : (target == Mode::AWAKE ? "AWAKE (charging)" : "MINIMAL");
    Serial.printf("[PWR] Mode -> %s (12V=%.1f)\n", name, _car_12v);
}

bool PowerManager::should_sleep() const {
    return _mode != Mode::ACTIVE;
}

void PowerManager::enter_sleep(uint32_t seconds) {
    Serial.printf("[PWR] Entering deep sleep for %u seconds\n", seconds);
    esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);

    // Ignition signal also wakes the device so it can start in ACTIVE mode.
    esp_sleep_enable_ext1_wakeup(1ULL << IGNITION_PIN,
                                 IGNITION_ACTIVE_LEVEL == HIGH ? ESP_EXT1_WAKEUP_ANY_HIGH
                                                               : ESP_EXT1_WAKEUP_ANY_LOW);
    if (IGNITION_ACTIVE_LEVEL == HIGH) {
        rtc_gpio_pulldown_en((gpio_num_t)IGNITION_PIN);
    } else {
        rtc_gpio_pullup_en((gpio_num_t)IGNITION_PIN);
    }
    esp_deep_sleep_start();
}
