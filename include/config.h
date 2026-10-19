#pragma once

#include <Arduino.h>
#include "secrets.h"

// ============================================================================
// LilyGo T-SIM7080G-S3 Hardware Configuration
// ============================================================================
// GPIO pinout and hardware-specific settings for the LilyGo T-SIM7080G-S3 board.
// Reference: https://github.com/Xinyuan-LilyGO/LilyGo-T-SIM7080G

// --- Device Info ---
#define DEVICE_NAME "OBD2MQTTS-ESP32"
#define DEVICE_HOSTNAME "obd2mqtts"
#define DEVICE_MODEL "LilyGo-T-SIM7080G-S3"

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
#define AXP2101_I2C_SDA_PIN   15     // I2C Data (GPIO 41/42 are modem PWRKEY/DTR)
#define AXP2101_I2C_SCL_PIN   7      // I2C Clock
#define AXP2101_IRQ_PIN       6      // Interrupt output

// Modem control (powered via AXP2101 DC3; PWRKEY pulse switches the modem on)
#define SIM7080_PWRKEY_PIN    41
#define SIM7080_DTR_PIN       42     // held LOW to keep the modem awake

// Cellular
#define MODEM_APN             "telenor"
#define MODEM_NETWORK_MODE    3      // AT+CMNB: 1=Cat-M, 2=NB-IoT, 3=both

// ADC Input for Car 12V Measurement
// Available ADC1 pins (not used by camera/SD): 9, 19, 20, etc.
// Use voltage divider (5.1k + 10k recommended for 0-5V range)
#define ADC_12V_PIN           10     // GPIO10 = ADC1_CHANNEL_9
#define ADC_12V_CHANNEL       ADC1_CHANNEL_9
#define ADC_12V_DIVIDER       5.278f // calibrate against a multimeter
// true = fixed 12.5 V (bench use); false = read ADC_12V_PIN
#define ADC_12V_SIMULATE      false

// Ignition sense input: ACTIVE while the level equals IGNITION_ACTIVE_LEVEL (also wakes from deep sleep)
#define IGNITION_PIN          1
#define IGNITION_ACTIVE_LEVEL HIGH

// ============================================================================
// POLLING INTERVALS
// ============================================================================

#define POLL_INTERVAL_ACTIVE_MS      2000    // BLE/OBD polling (local only, uses no mobile data)

// MQTT reporting over LTE (these cost mobile data)
#define REPORT_INTERVAL_ACTIVE_MS    60000   // Driving: telemetry every 1 min
#define REPORT_INTERVAL_CHARGING_MS  300000  // Charging: telemetry every 5 min
#define STATUS_INTERVAL_MS           600000  // Status + voltages every 10 min
#define REPORT_EVENT_SOC_DELTA       1.0f    // Send early when display SoC moves this many %
#define REPORT_EVENT_MIN_GAP_MS      15000   // Minimum spacing between early sends

// ============================================================================
// POWER & SLEEP SETTINGS
// ============================================================================

// Parked (MINIMAL): deep sleep, wake the modem, send voltages, sleep again
#define MINIMAL_REPORT_INTERVAL_SEC  21600   // 6 hours between parked reports
#define MINIMAL_CHECK_INTERVAL_SEC   300     // short wake to check 12V for charging (no radio)
#define MINIMAL_ONLINE_TIMEOUT_MS    180000  // give up and sleep if LTE is not up in 3 min

// LTE link upkeep while awake (data usage)
#define MODEM_LINK_CHECK_INTERVAL_MS 300000  // 5 min
#define MQTT_KEEPALIVE_S             120

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

// OBD adapter BLE address, e.g. "aa:bb:cc:dd:ee:ff". Empty = match the name "vLinker".
#define OBD_BLE_MAC ""

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
