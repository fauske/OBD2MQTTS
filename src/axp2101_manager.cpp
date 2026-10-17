#include "axp2101_manager.h"
#include "config.h"

Axp2101Manager::Axp2101Manager() {
}

bool Axp2101Manager::begin() {
    Serial.println("[AXP2101] Initializing power management IC...");

    if (!_pmu.begin(Wire, AXP2101_I2C_ADDR,
                    AXP2101_I2C_SDA_PIN, AXP2101_I2C_SCL_PIN)) {
        Serial.println("[AXP2101] Failed to initialize");
        return false;
    }

    Serial.println("[AXP2101] Initialized successfully");
    init_power_rails();
    read_voltages();
    _initialized = true;
    return true;
}

void Axp2101Manager::init_power_rails() {
    // DC3 supports up to 3.4 V on the AXP2101.
    _pmu.setDC3Voltage(3400);
    _pmu.enableDC3();

    // Enable ESP32 power rail (DCDC1, 3.3V)
    _pmu.setDC1Voltage(3300);
    _pmu.enableDC1();

    // Disable charging by default (NTC not present)
    _pmu.disableCellbatteryCharge();

    // Set power button: 128ms press = on, 6s press = off
    _pmu.setPowerKeyPressOnTime(XPOWERS_POWERON_128MS);
    _pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_6S);

    Serial.println("[AXP2101] Power rails initialized");
}

void Axp2101Manager::update() {
    if (!_initialized) return;

    if (millis() - _last_update_ms > 30000) {
        _last_update_ms = millis();
        read_voltages();
    }
}

float Axp2101Manager::get_vbus_voltage() const {
    return _vbus_v;
}

float Axp2101Manager::get_battery_voltage() const {
    return _batt_v;
}

float Axp2101Manager::get_vsys_voltage() const {
    return _vsys_v;
}

int Axp2101Manager::get_battery_percent() const {
    return _batt_percent;
}

bool Axp2101Manager::is_charging() const {
    return _is_charging;
}

bool Axp2101Manager::is_discharging() const {
    return !_is_charging && _batt_v > 0;
}

bool Axp2101Manager::is_connected() const {
    return _vbus_v > 4.0f;
}

void Axp2101Manager::read_voltages() {
    if (!_initialized) return;

    _vbus_v = _pmu.getVbusVoltage() / 1000.0f;
    _batt_v = _pmu.getBattVoltage() / 1000.0f;
    _vsys_v = _pmu.getSystemVoltage() / 1000.0f;
    _batt_percent = _pmu.getBatteryPercent();
    _is_charging = _pmu.isCharging();

    Serial.printf("[AXP2101] VBUS=%.2fV BATT=%.2fV VSYS=%.2fV SOC=%d%% CHG=%s\n",
                  _vbus_v, _batt_v, _vsys_v, _batt_percent,
                  _is_charging ? "YES" : "NO");
}

void Axp2101Manager::enable_modem_power(bool enable) {
    if (!_initialized) return;

    if (enable) {
        _pmu.enableDC3();
        Serial.println("[AXP2101] Modem power ON");
    } else {
        _pmu.disableDC3();
        Serial.println("[AXP2101] Modem power OFF");
    }
}

void Axp2101Manager::enable_esp_power(bool enable) {
    if (!_initialized) return;

    if (enable) {
        _pmu.enableDC1();
        Serial.println("[AXP2101] ESP power ON");
    } else {
        _pmu.disableDC1();
        Serial.println("[AXP2101] ESP power OFF");
    }
}

void Axp2101Manager::shutdown() {
    if (!_initialized) return;
    Serial.println("[AXP2101] Shutting down");
    _pmu.shutdown();
}
