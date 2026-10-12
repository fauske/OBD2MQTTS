#include "axp2101_manager.h"
#include "config.h"

#ifndef AXP2101_NTC_INSTALLED
#define AXP2101_NTC_INSTALLED 0
#endif

namespace {
constexpr uint8_t AXP2101_TS_CTRL_REG = 0x50;
constexpr uint8_t AXP2101_TS_CTRL_MASK = 0x1F;
constexpr uint8_t AXP2101_TS_NTC_50UA_WHILE_ADC = 0x06;
constexpr uint8_t AXP2101_COLD_CHARGE_THRESHOLD_5C_B3380 = 0x23;
constexpr uint8_t AXP2101_VLTF_CHG_REG = 0x54;
}

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
    _initialized = true;
    init_power_rails();
    if (!configure_battery_temperature_protection()) {
        _pmu.disableCellbatteryCharge();
        Serial.println("[AXP2101] Temperature protection setup failed; charging disabled");
        return false;
    }
    _last_update_ms = millis();
    read_voltages();
    return true;
}

void Axp2101Manager::init_power_rails() {
    // LilyGO specifies 3.0 V for the SIM7080 modem rail.
    _pmu.setDC3Voltage(3000);
    _pmu.enableDC3();

    // Enable ESP32 power rail (DCDC1, 3.3V)
    _pmu.setDC1Voltage(3300);
    _pmu.enableDC1();

    // BLDO1 powers the board's modem UART level converter; keep it enabled.
    _pmu.setBLDO1Voltage(3300);
    _pmu.enableBLDO1();

    // Disable charging by default (NTC not present)
    _pmu.disableCellbatteryCharge();

    // Set power button: 128ms press = on, 6s press = off
    _pmu.setPowerKeyPressOnTime(XPOWERS_POWERON_128MS);
    _pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_6S);

    Serial.println("[AXP2101] Power rails initialized");
}

bool Axp2101Manager::configure_battery_temperature_protection() {
    _pmu.disableCellbatteryCharge();
    const int charge_ctrl = _pmu.readRegister(XPOWERS_AXP2101_CHARGE_GAUGE_WDT_CTRL);
    if (charge_ctrl < 0 || (charge_ctrl & (1 << 1)) != 0) return false;

    if (!AXP2101_NTC_INSTALLED) {
        _pmu.disableTSPinMeasure();
        Serial.println("[AXP2101] Charging disabled until the battery NTC is installed");
        return true;
    }

    const int ts_ctrl = _pmu.readRegister(AXP2101_TS_CTRL_REG);
    if (ts_ctrl < 0) return false;

    const uint8_t ntc_ctrl =
        (static_cast<uint8_t>(ts_ctrl) & ~AXP2101_TS_CTRL_MASK) |
        AXP2101_TS_NTC_50UA_WHILE_ADC;
    if (_pmu.writeRegister(AXP2101_TS_CTRL_REG, ntc_ctrl) != 0) return false;
    if (!_pmu.enableTSPinMeasure()) return false;

    if (_pmu.writeRegister(AXP2101_VLTF_CHG_REG,
                           AXP2101_COLD_CHARGE_THRESHOLD_5C_B3380) != 0) {
        return false;
    }

    if (_pmu.readRegister(AXP2101_TS_CTRL_REG) != ntc_ctrl ||
        _pmu.readRegister(AXP2101_VLTF_CHG_REG) !=
            AXP2101_COLD_CHARGE_THRESHOLD_5C_B3380) {
        return false;
    }

    _pmu.enableCellbatteryCharge();
    const int enabled_charge_ctrl =
        _pmu.readRegister(XPOWERS_AXP2101_CHARGE_GAUGE_WDT_CTRL);
    if (enabled_charge_ctrl < 0 || (enabled_charge_ctrl & (1 << 1)) == 0) {
        return false;
    }

    Serial.println("[AXP2101] Battery NTC protection enabled; cold charge limit is approximately 5C");
    return true;
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
