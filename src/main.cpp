#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <esp_system.h>

#include "config.h"
#include "serial_diagnostics.h"
#include "data_store.h"
#include "modules/ble_transport.h"
#include "modules/obd_decoder.h"
#include "modules/modem_client.h"
#include "power_manager.h"
#include "ota_manager.h"
#include "axp2101_manager.h"

DataStore g_store;
BleTransport g_ble;
ObdDecoder g_obd(g_ble);
ModemClient g_modem;
PowerManager g_power;
OtaManager g_ota;
Axp2101Manager g_axp;

bool g_wifi_ota_active = false;
uint32_t g_wifi_ota_start_ms = 0;

#if ENABLE_SERIAL_DIAGNOSTICS
namespace {
uint32_t g_last_diagnostic_ms = 0;

const char *power_mode_name(PowerManager::Mode mode) {
    switch (mode) {
        case PowerManager::Mode::ACTIVE: return "ACTIVE";
        case PowerManager::Mode::AWAKE: return "AWAKE";
        case PowerManager::Mode::MINIMAL: return "MINIMAL";
    }
    return "UNKNOWN";
}

const char *ble_state_name(BleTransport::State state) {
    switch (state) {
        case BleTransport::State::IDLE: return "IDLE";
        case BleTransport::State::SCANNING: return "SCANNING";
        case BleTransport::State::FOUND: return "FOUND";
        case BleTransport::State::CONNECTING: return "CONNECTING";
        case BleTransport::State::DISCOVERING: return "DISCOVERING";
        case BleTransport::State::READY: return "READY";
        case BleTransport::State::DISCONNECTED: return "DISCONNECTED";
    }
    return "UNKNOWN";
}

const char *modem_state_name(ModemClient::State state) {
    switch (state) {
        case ModemClient::State::IDLE: return "IDLE";
        case ModemClient::State::INITIALIZING: return "INITIALIZING";
        case ModemClient::State::WAITING_NETWORK: return "WAITING_NETWORK";
        case ModemClient::State::CONNECTING_DATA: return "CONNECTING_DATA";
        case ModemClient::State::CONNECTING_MQTT: return "CONNECTING_MQTT";
        case ModemClient::State::READY: return "READY";
        case ModemClient::State::ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

void print_diagnostic_status() {
    const BmsData bms = g_obd.get_bms_data();
    const uint32_t bms_age_ms = bms.valid ? millis() - bms.last_update_ms : 0xFFFFFFFFUL;

    SERIAL_DIAG_PRINTF(
        "[DIAG] up=%lus reset=%d power=%s heap=%u AXP=%u VBUS=%.2fV BATT=%.2fV VSYS=%.2fV CHG=%u BLE=%s/%u OBD_READY=%u LTE_STATE=%s LTE_REG=%u LTE_DATA=%u CSQ=%d MQTT_ONLINE=%u MQTT_RC=%d WiFi_STATUS=%d BMS_VALID=%u BMS_AGE=%lums\n",
        static_cast<unsigned long>(millis() / 1000),
        static_cast<int>(esp_reset_reason()),
        power_mode_name(g_power.current_mode()),
        static_cast<unsigned>(ESP.getFreeHeap()),
        static_cast<unsigned>(g_axp.is_initialized()),
        g_axp.get_vbus_voltage(),
        g_axp.get_battery_voltage(),
        g_axp.get_vsys_voltage(),
        static_cast<unsigned>(g_axp.is_charging()),
        ble_state_name(g_ble.state()),
        static_cast<unsigned>(g_ble.is_connected()),
        static_cast<unsigned>(g_obd.is_ready()),
        modem_state_name(g_modem.state()),
        static_cast<unsigned>(g_modem.is_network_registered()),
        static_cast<unsigned>(g_modem.is_data_connected()),
        g_modem.signal_quality(),
        static_cast<unsigned>(g_modem.is_online()),
        g_modem.mqtt_state(),
        static_cast<int>(WiFi.status()),
        static_cast<unsigned>(bms.valid),
        static_cast<unsigned long>(bms_age_ms));
}
}
#endif

void setup() {
    Serial.begin(SERIAL_BAUD);
    WiFi.mode(WIFI_OFF);
#if ENABLE_SERIAL_DIAGNOSTICS
    const uint32_t serial_wait_started_ms = millis();
    while (!Serial && millis() - serial_wait_started_ms < 1500) {
        delay(10);
    }
    SERIAL_DIAG_PRINTF("\n[BOOT] OBD2MQTTS diagnostics enabled; reset_reason=%d heap=%u USB_CDC=%d\n",
                       static_cast<int>(esp_reset_reason()),
                       static_cast<unsigned>(ESP.getFreeHeap()),
#ifdef ARDUINO_USB_CDC_ON_BOOT
                       ARDUINO_USB_CDC_ON_BOOT
#else
                       0
#endif
    );
#endif
    delay(500);

    Serial.println("\n=== OBD2MQTTS boot ===");
    if (!g_axp.begin()) {
        Serial.println("[BOOT] AXP2101 initialization failed");
    }
    g_power.begin();

    if (g_power.current_mode() == PowerManager::Mode::ACTIVE) {
        Serial.println("[BOOT] ACTIVE mode: ignition ON");

        g_ble.begin();
        g_obd.begin();
        g_modem.begin();

        if (ENABLE_WIFI_OTA_WINDOW) {
            WiFi.mode(WIFI_STA);
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            g_ota.begin(DEVICE_HOSTNAME, OTA_PASSWORD, OTA_PORT);
            g_wifi_ota_active = true;
            g_wifi_ota_start_ms = millis();
        }
    } else {
        // Parked or charging state: use LTE only, no WiFi by default.
        Serial.printf("[BOOT] mode=%d\n", (int)g_power.current_mode());
        g_modem.begin();
    }

#if ENABLE_SERIAL_DIAGNOSTICS
    print_diagnostic_status();
    g_last_diagnostic_ms = millis();
#endif
}

void loop() {
    g_power.update();
    g_axp.update();

    if (g_wifi_ota_active && g_ota.is_ready()) {
        g_ota.handle();
    }

    if (g_wifi_ota_active && millis() - g_wifi_ota_start_ms > WIFI_OTA_WINDOW_MS) {
        Serial.println("[WiFi] OTA window expired, WiFi disabled");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        g_wifi_ota_active = false;
    }

    switch (g_power.current_mode()) {
        case PowerManager::Mode::ACTIVE:
            g_ble.update();
            g_obd.update();
            g_modem.update();

            g_store.set_ble_connected(g_ble.is_connected());
            g_store.set_obd_ready(g_obd.is_ready());
            g_store.set_mqtt_online(g_modem.is_online());

            static uint32_t last_active_pub = 0;
            if (millis() - last_active_pub >= REPORT_INTERVAL_MS) {
                last_active_pub = millis();
                BmsData b = g_obd.get_bms_data();
                if (b.valid) {
                    g_store.set_bms_data(b);
                    g_modem.publish_telemetry(b);
                }
                SystemStatus s = g_store.get_status();
                g_modem.publish_status(s);
            }
            break;

        case PowerManager::Mode::AWAKE:
            // Charging while ignition OFF: keep telemetry active, but less frequent
            g_obd.update();
            g_modem.update();
            g_store.set_obd_ready(g_obd.is_ready());
            g_store.set_mqtt_online(g_modem.is_online());

            static uint32_t last_awake_pub = 0;
            if (millis() - last_awake_pub >= POLL_INTERVAL_AWAKE_MS) {
                last_awake_pub = millis();
                BmsData b = g_obd.get_bms_data();
                if (b.valid) {
                    g_store.set_bms_data(b);
                    g_modem.publish_telemetry(b);
                }
            }
            break;

        case PowerManager::Mode::MINIMAL:
            // Parked/out-of-charge: publish a power sample only every hour.
            g_modem.update();
            static uint32_t last_minimal_pub = 0;
            if (millis() - last_minimal_pub >= POLL_INTERVAL_MINIMAL_MS) {
                last_minimal_pub = millis();
                PowerData power;
                power.car_12v = g_power.get_car_12v();
                power.board_v = g_power.get_board_v();
                g_modem.publish_power(power);
            }

            // Keep USB serial available while diagnostics are enabled.
#if !ENABLE_SERIAL_DIAGNOSTICS
            if (!g_modem.is_online()) {
                Serial.println("[MAIN] Sleeping for deep-sleep window");
                g_power.enter_sleep(DEEP_SLEEP_DURATION_SEC);
            }
#endif
            break;
    }

#if ENABLE_SERIAL_DIAGNOSTICS
    if (millis() - g_last_diagnostic_ms >= 5000) {
        g_last_diagnostic_ms = millis();
        print_diagnostic_status();
    }
#endif

    delay(50);
}
