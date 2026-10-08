#pragma once

// -----------------------------------------------------------------------------
// Public project config.
// This file is safe to commit because it contains only placeholders.
// For real production values, copy these into a local untracked config or use
// a secure secret-store pattern.
// -----------------------------------------------------------------------------

#define DEVICE_NAME "OBD2MQTTS-ESP32"
#define DEVICE_HOSTNAME "obd2mqtts"

#define WIFI_SSID "CHANGE_ME"
#define WIFI_PASSWORD "CHANGE_ME"

// --- MQTT over TLS (MQTTS) ---
#define MQTT_HOST "mqtt.example.com"
#define MQTT_PORT 8883                    // MQTTS secure port
#define MQTT_USE_TLS 1                    // 1 = MQTTS (TLS/SSL), 0 = plain MQTT
#define MQTT_CLIENT_ID "obd2mqtts-client"
#define MQTT_USERNAME "CHANGE_ME"
#define MQTT_PASSWORD "CHANGE_ME"
#define MQTT_TOPIC_TELEMETRY "obd2/telemetry"
#define MQTT_TOPIC_STATUS "obd2/status"

// --- Optional: MQTT CA certificate for server verification ---
// Leave empty ("") to skip server certificate verification (encrypted but unverified).
// For production, paste your broker's CA certificate in PEM format.
#define MQTT_CA_CERT ""

#define OTA_PASSWORD "CHANGE_ME"
#define OTA_PORT 3232

#define SERIAL_BAUD 115200
#define CHECK_INTERVAL_MS 1000
#define REPORT_INTERVAL_MS 5000

#define ENABLE_MQTT true
#define ENABLE_OTA true
