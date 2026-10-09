#include "../../include/power_manager.h"
#include "../include/config.h"
#include <esp_sleep.h>
#include <Wire.h>
#include <XPowersLib.h>

static XPowersAXP2101 g_pmu;

PowerManager::PowerManager() {
}

void PowerManager::begin() {
    Serial.println("[PWR] Initializing power manager...");

    pinMode(ADC_12V_PIN, INPUT);
    pinMode(MODEM_PWRKEY_PIN, OUTPUT);
    digitalWrite(MODEM_PWRKEY_PIN, LOW);
    pinMode(MODEM_DTR_PIN, OUTPUT);
    digitalWrite(MODEM_DTR_PIN, LOW);
    pinMode(MODEM_RI_PIN, INPUT);

    _pmu_ok = init_pmu();

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

bool PowerManager::init_pmu() {
    Wire.begin(PMU_I2C_SDA, PMU_I2C_SCL);
    if (!g_pmu.begin(Wire, AXP2101_ADDR, PMU_I2C_SDA, PMU_I2C_SCL)) {
        Serial.println("[PWR] AXP2101 not found");
        return false;
    }
    // No NTC on the board: TS detection must be off or charging is disabled.
    g_pmu.disableTSPinMeasure();
    g_pmu.enableBattVoltageMeasure();
    g_pmu.enableVbusVoltageMeasure();
    g_pmu.enableSystemVoltageMeasure();
    g_pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
    g_pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);
    Serial.println("[PWR] AXP2101 initialized");
    return true;
}

void PowerManager::modem_power_on() {
    if (_pmu_ok) {
        g_pmu.setDC3Voltage(PMU_MODEM_DC3_MV);
        g_pmu.enableDC3();
        g_pmu.setBLDO1Voltage(PMU_LEVEL_BLDO1_MV);
        g_pmu.enableBLDO1();
        delay(100);  // let the rail stabilise
    }
    // PWRKEY pulse: LOW -> HIGH (~1 s) -> LOW
    digitalWrite(MODEM_PWRKEY_PIN, LOW);
    delay(100);
    digitalWrite(MODEM_PWRKEY_PIN, HIGH);
    delay(MODEM_PWRKEY_PULSE_MS);
    digitalWrite(MODEM_PWRKEY_PIN, LOW);
}

void PowerManager::modem_power_off() {
    if (_pmu_ok) {
        g_pmu.disableDC3();
        g_pmu.disableBLDO1();
    }
}

bool PowerManager::ignition_on() const {
    return _car_12v > IGNITION_ON_THRESHOLD;
}

void PowerManager::read_voltages() {
    // Placeholder: real ADC implementation should be used later.
    // For now, simulated values intentionally keep the logic testable.
    _car_12v = 12.5f;
    if (_pmu_ok) {
        _batt_v = g_pmu.getBattVoltage() / 1000.0f;
        _batt_pct = g_pmu.getBatteryPercent();
        _board_v = g_pmu.getSystemVoltage() / 1000.0f;
    } else {
        _board_v = 4.2f;
    }
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
