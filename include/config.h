#pragma once

#include <Arduino.h>

// ============================================================================
// Runtime configuration values for BLE + OBD + LTE telemetry.
// ============================================================================
// Keep these as simple macros for easy tweaking at runtime without changing code.
// All values are in milliseconds unless otherwise noted.

#define DEVICE_NAME "OBD2MQTTS-ESP32"
#define DEVICE_HOSTNAME "obd2mqtts"

#define WIFI_SSID "CHANGE_ME"
#define WIFI_PASSWORD "CHANGE_ME"

// --- MQTT over TLS (MQTTS) ---
#define MQTT_HOST "mqtt.example.com"
#define MQTT_PORT 8883
#define MQTT_USE_TLS 1
#define MQTT_CLIENT_ID "obd2mqtts-client"
#define MQTT_USERNAME "CHANGE_ME"
#define MQTT_PASSWORD "CHANGE_ME"
#define MQTT_TOPIC_TELEMETRY "obd2/telemetry"
#define MQTT_TOPIC_STATUS "obd2/status"
#define MQTT_CA_CERT ""

#define OTA_PASSWORD "CHANGE_ME"
#define OTA_PORT 3232

#define SERIAL_BAUD 115200

// --- LilyGo T-SIM7080G-S3 board pinout ---
// Source: Xinyuan-LilyGO/LilyGo-T-SIM7080G (LILYGO_ESP32S3_CAM_SIM7080G utilities.h)
// SIM7080G supports only LTE Cat-M and NB-IoT (no 2G/3G/4G): use a matching SIM.
// Insert the SIM before the modem is powered on.
#define MODEM_UART_NUM        1       // Serial1
#define MODEM_BAUD            115200
#define MODEM_RX_PIN          4       // ESP RX <- modem TXD
#define MODEM_TX_PIN          5       // ESP TX -> modem RXD
#define MODEM_PWRKEY_PIN      41      // Modem PWRKEY (pulse to power on)
#define MODEM_DTR_PIN         42      // Modem DTR (sleep control)
#define MODEM_RI_PIN          3       // Modem RI (input)
#define MODEM_RST_PIN         -1      // No reset GPIO: use AT+CFUN=1,1 / PWRKEY / PMU rail cycle
#define MODEM_PWRKEY_PULSE_MS 1000

// AXP2101 PMU (I2C)
#define PMU_I2C_SDA           15
#define PMU_I2C_SCL           7
#define PMU_IRQ_PIN           6
#define AXP2101_ADDR          0x34
#define PMU_MODEM_DC3_MV      3000    // Modem supply rail (DC3)
#define PMU_LEVEL_BLDO1_MV    3300    // Level shifter rail (BLDO1)
#define PMU_CHARGE_CURRENT_MA 500     // Battery charge current (500 mA)

// 12V sense input: must be a free ADC1 pin (not used by modem/PMU/camera/SD).
// GPIOs 1-18, 21, 38-42, 47, 48 are used by board peripherals when camera/SD
// are attached; adjust if your wiring differs. Use a divider + TVS/Zener.
#define ADC_12V_PIN           10

// Polling intervals
#define POLL_INTERVAL_ACTIVE_MS      2000    // Full telemetry while ignition is ON
#define POLL_INTERVAL_AWAKE_MS       10000   // Charging state while ignition is OFF but charging
#define POLL_INTERVAL_MINIMAL_MS     3600000 // Parked state: 60 minutes between sends
#define REPORT_INTERVAL_MS           2500    // Publish cadence while active
#define STATUS_INTERVAL_MS           30000   // Health/status updates

// Sleep settings
#define DEEP_SLEEP_DURATION_SEC       3600    // 1 hour sleep period

// Feature toggles
#define ENABLE_MQTT true
#define ENABLE_OTA true
#define ENABLE_WIFI_OTA_WINDOW true
#define WIFI_OTA_WINDOW_MS 600000  // 10 minutes at ignition ON
