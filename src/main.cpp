#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>

#include "config.h"
#include "data_store.h"
#include "modules/ble_transport.h"
#include "modules/obd_decoder.h"
#include "modules/modem_client.h"
#include "power_manager.h"
#include "ota_manager.h"

DataStore g_store;
BleTransport g_ble;
ObdDecoder g_obd(g_ble);
ModemClient g_modem;
PowerManager g_power;
OtaManager g_ota;

bool g_wifi_ota_active = false;
uint32_t g_wifi_ota_start_ms = 0;

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);

    Serial.println("\n=== OBD2MQTTS boot ===");
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
}

void loop() {
    g_power.update();

    if (g_wifi_ota_active && g_ota.is_ready()) {
        g_ota.handle();
    }

    if (g_wifi_ota_active && millis() - g_wifi_ota_start_ms > WIFI_OTA_WINDOW_MS) {
        Serial.println("[WiFi] OTA window expired, WiFi disabled");
        WiFi.disconnect(true);
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

            // Sleep if idle and no charging
            if (!g_modem.is_online()) {
                Serial.println("[MAIN] Sleeping for deep-sleep window");
                g_power.enter_sleep(DEEP_SLEEP_DURATION_SEC);
            }
            break;
    }

    delay(50);
}
