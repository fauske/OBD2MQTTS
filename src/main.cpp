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
bool g_obd_started = false;
PowerManager::Mode g_last_power_mode = PowerManager::Mode::MINIMAL;
bool g_trip_publish_pending = false;
bool g_gnss_publish_pending = false;

void update_system_status() {
    g_store.set_ble_connected(g_ble.is_connected());
    g_store.set_obd_ready(g_obd.is_ready());
    g_store.set_modem_ready(g_modem.is_data_connected());
    g_store.set_mqtt_online(g_modem.is_online());

    const int csq = g_modem.signal_quality();
    const int8_t rssi_dbm = csq > 0 && csq <= 31
        ? static_cast<int8_t>(-113 + 2 * csq)
        : 0;
    g_store.set_rssi(rssi_dbm);
    g_store.set_uptime_s(millis() / 1000);
}

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
    g_last_power_mode = g_power.current_mode();

    if (g_power.current_mode() == PowerManager::Mode::ACTIVE) {
        Serial.println("[BOOT] ACTIVE mode: ignition ON");

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

    if (!g_obd_started && g_power.current_mode() != PowerManager::Mode::MINIMAL) {
        g_ble.begin();
        g_obd.begin();
        g_obd_started = true;
    }

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
            g_modem.set_gnss_enabled(true);
            g_modem.update();

            static uint32_t last_gnss_poll_ms = 0;
            if (g_modem.gnss_ready() &&
                millis() - last_gnss_poll_ms >= GNSS_POLL_INTERVAL_MS) {
                last_gnss_poll_ms = millis();
                GnssData gnss;
                if (g_modem.read_gnss(gnss)) {
                    g_store.set_gnss_data(gnss);
                }
            }

            static uint32_t last_active_pub = 0;
            if (millis() - last_active_pub >= REPORT_INTERVAL_MS) {
                last_active_pub = millis();
                BmsData b = g_obd.get_bms_data();
                if (b.valid && g_obd.have_fresh_data()) {
                    g_store.set_bms_data(b);
                    g_modem.publish_telemetry(b);
                }
            }
            break;

        case PowerManager::Mode::AWAKE:
            // Charging while ignition OFF: keep telemetry active, but less frequent
            g_ble.update();
            g_obd.update();
            g_modem.set_gnss_enabled(false);
            g_modem.update();
            static uint32_t last_awake_pub = 0;
            if (millis() - last_awake_pub >= POLL_INTERVAL_AWAKE_MS) {
                last_awake_pub = millis();
                BmsData b = g_obd.get_bms_data();
                if (b.valid && g_obd.have_fresh_data()) {
                    g_store.set_bms_data(b);
                    g_modem.publish_telemetry(b);
                }
            }
            break;

        case PowerManager::Mode::MINIMAL:
            g_modem.set_gnss_enabled(false);
            g_modem.update();
            if (g_obd.trip_query_pending()) {
                g_ble.update();
                g_obd.update();
            }

            // Keep USB serial available while diagnostics are enabled.
#if !ENABLE_SERIAL_DIAGNOSTICS
            if (!g_modem.is_online() && !g_obd.trip_query_pending()) {
                Serial.println("[MAIN] Sleeping for deep-sleep window");
                g_power.enter_sleep(DEEP_SLEEP_DURATION_SEC);
            }
#endif
            break;
    }

    const PowerManager::Mode current_power_mode = g_power.current_mode();
    if (current_power_mode == PowerManager::Mode::ACTIVE) {
        g_trip_publish_pending = false;
        g_gnss_publish_pending = false;
    } else if (g_last_power_mode == PowerManager::Mode::ACTIVE) {
        if (g_obd_started) {
            g_obd.request_trip_queries();
            g_trip_publish_pending = true;
        }
        g_gnss_publish_pending = true;
    }
    g_last_power_mode = current_power_mode;

    static uint32_t last_trip_publish_attempt_ms = 0;
    if (g_trip_publish_pending && g_obd.trip_queries_complete() &&
        g_modem.is_online() &&
        millis() - last_trip_publish_attempt_ms >= STATUS_INTERVAL_MS) {
        last_trip_publish_attempt_ms = millis();
        const TripData trip = g_obd.get_trip_data();
        g_store.set_trip_data(trip);
        if (g_modem.publish_trip(trip)) {
            g_trip_publish_pending = false;
        }
    }

    static uint32_t last_gnss_publish_attempt_ms = 0;
    if (g_gnss_publish_pending && g_modem.is_online() &&
        millis() - last_gnss_publish_attempt_ms >= STATUS_INTERVAL_MS) {
        last_gnss_publish_attempt_ms = millis();
        const GnssData gnss = g_store.get_gnss_data();
        if (!gnss.valid || millis() - gnss.last_update_ms > GNSS_POLL_INTERVAL_MS * 3) {
            g_gnss_publish_pending = false;
        } else if (g_modem.publish_gnss(gnss)) {
            g_gnss_publish_pending = false;
        }
    }

    static PowerManager::Mode previous_power_mode = current_power_mode;
    static uint32_t last_power_pub_ms = 0;
    const bool power_mode_changed = current_power_mode != previous_power_mode;

    const uint32_t power_report_interval_ms =
        current_power_mode == PowerManager::Mode::MINIMAL
            ? ((AXP2101_ENABLE_BATTERY_CHARGING &&
                AXP2101_ALLOW_UNVERIFIED_BENCH_CHARGING &&
                !AXP2101_NTC_PROTECTION_VERIFIED)
                    ? STATUS_INTERVAL_MS
                    : POLL_INTERVAL_MINIMAL_MS)
            : STATUS_INTERVAL_MS;
    if (g_modem.is_online() &&
        (power_mode_changed || millis() - last_power_pub_ms >= power_report_interval_ms)) {
        previous_power_mode = current_power_mode;
        last_power_pub_ms = millis();
        PowerData power;
        power.car_12v = g_power.get_car_12v();
        power.board_v = g_axp.get_battery_voltage();
        power.vbus_v = g_axp.get_vbus_voltage();
        power.vsys_v = g_axp.get_vsys_voltage();
        power.battery_percent = g_axp.get_battery_percent();
        power.battery_present = g_axp.is_battery_present();
        power.charging = g_axp.is_charging();
        power.ts_adc_raw = g_axp.get_ts_adc_raw();
        g_modem.publish_power(power);
    }

    update_system_status();
    static uint32_t last_status_pub_ms = 0;
    if (g_modem.is_online() && millis() - last_status_pub_ms >= STATUS_INTERVAL_MS) {
        last_status_pub_ms = millis();
        g_modem.publish_status(g_store.get_status());
    }

#if ENABLE_SERIAL_DIAGNOSTICS
    if (millis() - g_last_diagnostic_ms >= 5000) {
        g_last_diagnostic_ms = millis();
        print_diagnostic_status();
    }
#endif

    delay(50);
}
