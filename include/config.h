#pragma once

#include <Arduino.h>

// ============================================================================
// LilyGo T-SIM7080G-S3 Hardware Configuration
// ============================================================================
// GPIO pinout and hardware-specific settings for the LilyGo T-SIM7080G-S3 board.
// Reference: https://github.com/Xinyuan-LilyGO/LilyGo-T-SIM7080G

// --- Device Info ---
#define DEVICE_NAME "OBD2MQTTS-ESP32"
#define DEVICE_HOSTNAME "obd2mqtts"
#define DEVICE_MODEL "LilyGo-T-SIM7080G-S3"

// --- WiFi Configuration ---
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

// --- OTA Configuration ---
#define OTA_PASSWORD "CHANGE_ME"
#define OTA_PORT 3232

// --- Serial Configuration ---
#define SERIAL_BAUD 115200

// ============================================================================
// GPIO PINOUT (LilyGo T-SIM7080G-S3)
// ============================================================================

// SIM7080 Modem UART (UART1, 115200 8N1)
#define SIM7080_UART_NUM      1
#define SIM7080_RX_PIN        4      // SIM7080 TX -> ESP32 RX
#define SIM7080_TX_PIN        5      // SIM7080 RX -> ESP32 TX
#define SIM7080_BAUD_RATE     115200

// AXP2101 Power Management IC (I2C)
#define AXP2101_I2C_ADDR      0x34
#define AXP2101_I2C_SDA_PIN   42     // I2C Data
#define AXP2101_I2C_SCL_PIN   41     // I2C Clock
#define AXP2101_IRQ_PIN       6      // Interrupt output

// Modem Power Control (via GPIO)
// Note: Modem is powered via AXP2101 DC-rail + GPIO pulse on PWRKEY
#define SIM7080_PWRKEY_PIN    -1     // TODO: Verify actual pin if available

// ADC Input for Car 12V Measurement
// Available ADC1 pins (not used by camera/SD): 9, 19, 20, etc.
// Use voltage divider (5.1k + 10k recommended for 0-5V range)
#define ADC_12V_PIN           9      // GPIO9 = ADC1_CHANNEL_8
#define ADC_12V_CHANNEL       ADC1_CHANNEL_8
#define ADC_12V_DIVIDER       5.278f // (10k + 5.1k) / 5.1k

// ============================================================================
// POLLING INTERVALS
// ============================================================================

#define POLL_INTERVAL_ACTIVE_MS      2000    // Ignition ON: 2 sec polling
#define POLL_INTERVAL_AWAKE_MS       10000   // Charging: 10 sec polling
#define POLL_INTERVAL_MINIMAL_MS     3600000 // Parked: 60 min polling
#define REPORT_INTERVAL_MS           2500    // MQTT publish cadence (active)
#define STATUS_INTERVAL_MS           30000   // Status updates

// ============================================================================
// POWER & SLEEP SETTINGS
// ============================================================================

#define DEEP_SLEEP_DURATION_SEC      3600    // 1 hour between parked samples

// Ignition detection thresholds
#define IGNITION_ON_THRESHOLD_V      12.0f   // Car running
#define IGNITION_OFF_THRESHOLD_V     11.5f   // Car off
#define IGNITION_DEBOUNCE_MS         5000    // 5 sec stability

// Charging detection thresholds
#define CHARGING_DETECT_THRESHOLD_V  13.5f   // Charging detected
#define CHARGING_RELEASE_THRESHOLD_V 13.0f   // Charging ended

// ============================================================================
// FEATURE TOGGLES
// ============================================================================

#define ENABLE_MQTT true
#define ENABLE_OTA true
#define ENABLE_WIFI_OTA_WINDOW true
#define WIFI_OTA_WINDOW_MS 600000  // 10 minutes at ignition ON

#define ENABLE_AXP2101 true         // Power management IC
#define ENABLE_BLE true             // Bluetooth Low Energy (ELM327)
#define ENABLE_SIM7080 true         // LTE modem

// ============================================================================
// IMPORTANT NOTES
// ============================================================================
// 1. SIM card MUST be inserted before powering on the modem
// 2. SIM7080G supports NB-IoT and Cat-M only (not 4G/LTE)
// 3. Verify ThingsMobile SIM supports Cat-M or NB-IoT
// 4. USB-C = ESP32 programming, Micro-USB = SIM7080 firmware updates
// 5. When not using USB, set USB CDC On Boot to DISABLE in Arduino settings
// 6. Camera and SD card use many GPIO pins - avoid if not needed
// 7. Deep sleep draws ~360µA (very efficient)
