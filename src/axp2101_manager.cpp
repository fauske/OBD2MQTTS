#include "axp2101_manager.h"
#include "config.h"

#ifndef AXP2101_NTC_INSTALLED
#define AXP2101_NTC_INSTALLED 0
#endif
#ifndef AXP2101_ENABLE_BATTERY_CHARGING
#define AXP2101_ENABLE_BATTERY_CHARGING 0
#endif
#ifndef AXP2101_NTC_PROTECTION_VERIFIED
#define AXP2101_NTC_PROTECTION_VERIFIED 0
#endif
#ifndef AXP2101_ALLOW_UNVERIFIED_BENCH_CHARGING
#define AXP2101_ALLOW_UNVERIFIED_BENCH_CHARGING 0
#endif

namespace {
constexpr uint8_t AXP2101_TS_CTRL_REG = 0x50;
constexpr uint8_t AXP2101_TS_CTRL_MASK = 0x1F;
constexpr uint8_t AXP2101_TS_NTC_CONFIG_50UA = 0x07;
constexpr uint8_t AXP2101_COLD_CHARGE_THRESHOLD_5C_B3380 = 0x23;
constexpr uint8_t AXP2101_VLTF_CHG_REG = 0x54;
constexpr uint16_t AXP2101_TS_ADC_MIN_VALID = 100;
constexpr uint16_t AXP2101_TS_ADC_MAX_VALID = 5000;
constexpr uint16_t AXP2101_TS_DERATE_TO_100MA = 1550;
constexpr uint16_t AXP2101_TS_ALLOW_1000MA = 1450;

bool is_ts_adc_plausible(uint16_t raw) {
    return raw >= AXP2101_TS_ADC_MIN_VALID && raw <= AXP2101_TS_ADC_MAX_VALID;
}
}

Axp2101Manager::Axp2101Manager() {
}

bool Axp2101Manager::set_charge_current(uint8_t current) {
    if (!_pmu.setChargerConstantCurr(current) ||
        _pmu.getChargerConstantCurr() != current) {
        return false;
    }

    _charge_current_setting = current;
    Serial.printf("[AXP2101] Charge current set to %umA\n",
                  current == XPOWERS_AXP2101_CHG_CUR_1000MA ? 1000U : 100U);
    return true;
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
    _last_ts_check_ms = millis();
    read_voltages();
    return true;
}

void Axp2101Manager::init_power_rails() {
    _pmu.setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_4V36);
    if (!_pmu.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_500MA) ||
        _pmu.getVbusVoltageLimit() != XPOWERS_AXP2101_VBUS_VOL_LIM_4V36 ||
        _pmu.getVbusCurrentLimit() != XPOWERS_AXP2101_VBUS_CUR_LIM_500MA) {
        Serial.println("[AXP2101] VBUS input limit configuration failed");
    } else {
        Serial.println("[AXP2101] VBUS input configured: 4.36V minimum, 500mA maximum");
    }

    if (!_pmu.enableBattDetection()) {
        Serial.println("[AXP2101] Battery detection enable failed");
    }
    if (!_pmu.enableBattVoltageMeasure()) {
        Serial.println("[AXP2101] Battery voltage ADC enable failed");
    }
    if (!_pmu.enableVbusVoltageMeasure()) {
        Serial.println("[AXP2101] VBUS voltage ADC enable failed");
    }
    if (!_pmu.enableSystemVoltageMeasure()) {
        Serial.println("[AXP2101] VSYS voltage ADC enable failed");
    }

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
        AXP2101_TS_NTC_CONFIG_50UA;
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

    if (!AXP2101_ENABLE_BATTERY_CHARGING ||
        (!AXP2101_NTC_PROTECTION_VERIFIED &&
         !AXP2101_ALLOW_UNVERIFIED_BENCH_CHARGING)) {
        Serial.println("[AXP2101] NTC configured; battery charging disabled until enabled and cutoff verified");
        log_ts_diagnostics();
        return true;
    }

    const uint16_t initial_ts_adc = _pmu.getTsTemperature();
    if (!is_ts_adc_plausible(initial_ts_adc)) {
        Serial.printf("[AXP2101] TS ADC invalid (%u); battery charging remains disabled\n",
                      static_cast<unsigned>(initial_ts_adc));
        return true;
    }
    if (!_pmu.isBatteryConnect()) {
        Serial.println("[AXP2101] Battery not detected; charging remains disabled");
        return true;
    }

    const uint8_t requested_charge_current =
        AXP2101_NTC_PROTECTION_VERIFIED &&
        initial_ts_adc <= AXP2101_TS_ALLOW_1000MA
            ? XPOWERS_AXP2101_CHG_CUR_1000MA
            : XPOWERS_AXP2101_CHG_CUR_100MA;
    if (!set_charge_current(requested_charge_current)) {
        Serial.println("[AXP2101] Could not verify requested charge-current limit");
        return false;
    }
    if (!AXP2101_NTC_PROTECTION_VERIFIED) {
        Serial.println("[AXP2101] WARNING: unverified NTC cutoff; supervised 100mA bench test only");
    }

    _pmu.enableCellbatteryCharge();
    const int enabled_charge_ctrl =
        _pmu.readRegister(XPOWERS_AXP2101_CHARGE_GAUGE_WDT_CTRL);
    if (enabled_charge_ctrl < 0 || (enabled_charge_ctrl & (1 << 1)) == 0) {
        return false;
    }
    _charging_enabled = true;

    Serial.println("[AXP2101] Charging enabled; TS-based current control active");
    return true;
}

void Axp2101Manager::update() {
    if (!_initialized) return;

    if (_charging_enabled && millis() - _last_ts_check_ms >= 1000) {
        _last_ts_check_ms = millis();
        const uint16_t ts_adc = _pmu.getTsTemperature();
        const bool battery_detected = _pmu.isBatteryConnect();
        if (!is_ts_adc_plausible(ts_adc) || !battery_detected) {
            _pmu.disableCellbatteryCharge();
            _charging_enabled = false;
            _charge_current_setting = 0xFF;
            const int charge_ctrl =
                _pmu.readRegister(XPOWERS_AXP2101_CHARGE_GAUGE_WDT_CTRL);
            Serial.printf("[AXP2101] Charge safety check failed (TS ADC=%u, battery detected=%u); charge bit=%u\n",
                          static_cast<unsigned>(ts_adc),
                          static_cast<unsigned>(battery_detected),
                          static_cast<unsigned>(charge_ctrl >= 0 &&
                              (charge_ctrl & (1 << 1)) != 0));
        } else if (AXP2101_NTC_PROTECTION_VERIFIED) {
            uint8_t requested_charge_current = _charge_current_setting;
            if (_charge_current_setting == XPOWERS_AXP2101_CHG_CUR_1000MA &&
                ts_adc >= AXP2101_TS_DERATE_TO_100MA) {
                requested_charge_current = XPOWERS_AXP2101_CHG_CUR_100MA;
            } else if (_charge_current_setting == XPOWERS_AXP2101_CHG_CUR_100MA &&
                       ts_adc <= AXP2101_TS_ALLOW_1000MA) {
                requested_charge_current = XPOWERS_AXP2101_CHG_CUR_1000MA;
            }

            if (requested_charge_current != _charge_current_setting &&
                !set_charge_current(requested_charge_current)) {
                _pmu.disableCellbatteryCharge();
                _charging_enabled = false;
                _charge_current_setting = 0xFF;
                Serial.println("[AXP2101] Charge-current update failed; charging disabled");
            }
        }
    }

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
    _battery_present = _pmu.isBatteryConnect();

    Serial.printf("[AXP2101] VBUS=%.2fV BATT=%.2fV VSYS=%.2fV SOC=%d%% CHG=%s\n",
                  _vbus_v, _batt_v, _vsys_v, _batt_percent,
                  _is_charging ? "YES" : "NO");
    if (AXP2101_NTC_INSTALLED) {
        log_ts_diagnostics();
    }
}

void Axp2101Manager::log_ts_diagnostics() {
    const int adc_ctrl = _pmu.readRegister(XPOWERS_AXP2101_ADC_CHANNEL_CTRL);
    const int ts_ctrl = _pmu.readRegister(AXP2101_TS_CTRL_REG);
    const int raw_high = _pmu.readRegister(XPOWERS_AXP2101_ADC_DATA_RELUST2);
    const int raw_low = _pmu.readRegister(XPOWERS_AXP2101_ADC_DATA_RELUST3);
    if (adc_ctrl < 0 || ts_ctrl < 0 || raw_high < 0 || raw_low < 0) {
        Serial.println("[AXP2101] TS register read failed");
        return;
    }

    const uint16_t ts_raw =
        (static_cast<uint16_t>(raw_high & 0x3F) << 8) |
        static_cast<uint16_t>(raw_low & 0xFF);
    _ts_adc_raw = ts_raw;
    Serial.printf("[AXP2101] TS ADC=%u (raw), ADC_CTRL=0x%02X TS_CTRL=0x%02X REG36=0x%02X REG37=0x%02X\n",
                  static_cast<unsigned>(ts_raw),
                  static_cast<unsigned>(adc_ctrl),
                  static_cast<unsigned>(ts_ctrl),
                  static_cast<unsigned>(raw_high),
                  static_cast<unsigned>(raw_low));
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
