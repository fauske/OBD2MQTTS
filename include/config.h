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
