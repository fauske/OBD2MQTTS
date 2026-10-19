#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>
#include <esp_ota_ops.h>
#include <esp_sleep.h>

#include "config.h"
#include "axp2101_manager.h"
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
Axp2101Manager g_axp;

PowerManager::Mode g_boot_mode = PowerManager::Mode::MINIMAL;
uint32_t g_minimal_start_ms = 0;
bool g_minimal_sent = false;
bool g_minimal_closing = false;

bool g_wifi_ota_active = false;
uint32_t g_wifi_ota_start_ms = 0;
bool g_ota_boot_validation_pending = false;
uint32_t g_ota_boot_validation_start_ms = 0;

// Seconds of parked sleep since the last LTE report; survives deep sleep.
RTC_DATA_ATTR uint32_t g_rtc_slept_s = 0;

void setup() {
    Serial.begin(SERIAL_BAUD);
    const bool timer_wake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
    // Native USB CDC: wait for the host so early boot logs are not lost (skipped on timer wake).
    if (!timer_wake) {
        for (uint32_t t = millis(); !Serial && millis() - t < 5000;) delay(10);
        delay(500);
    }
    // Without a USB host, HWCDC writes would otherwise block on every log line.
    if (!Serial) Serial.setTxTimeoutMs(0);

    Serial.println("\n=== OBD2MQTTS boot ===");
    g_power.begin();
    g_boot_mode = g_power.current_mode();
    g_minimal_start_ms = millis();

    if (g_boot_mode == PowerManager::Mode::MINIMAL) {
        // Short wake: only read 12V/ignition. The radio is used once per MINIMAL_REPORT_INTERVAL_SEC.
        g_rtc_slept_s = timer_wake ? g_rtc_slept_s + MINIMAL_CHECK_INTERVAL_SEC
                                   : MINIMAL_REPORT_INTERVAL_SEC;
        if (g_rtc_slept_s < MINIMAL_REPORT_INTERVAL_SEC) {
            g_power.enter_sleep(MINIMAL_CHECK_INTERVAL_SEC);
        }
        g_rtc_slept_s = 0;
    }

    if (!g_axp.begin()) {
        Serial.println("[BOOT] AXP2101 not available; relying on default power rails");
    }
    g_axp.enable_modem_power(true);

    if (g_boot_mode != PowerManager::Mode::MINIMAL) {
        Serial.printf("[BOOT] %s mode\n",
                      g_boot_mode == PowerManager::Mode::ACTIVE ? "ACTIVE (ignition)" : "CHARGING");

        g_ble.begin();
        g_obd.begin();
        g_modem.begin();

        if (ENABLE_WIFI_OTA_WINDOW && g_boot_mode == PowerManager::Mode::ACTIVE) {
            WiFi.mode(WIFI_STA);
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            g_ota.begin(DEVICE_HOSTNAME, OTA_PASSWORD, OTA_PORT);
            g_wifi_ota_active = true;
            g_wifi_ota_start_ms = millis();
        }
    } else {
        // Parked: LTE only, no BLE or WiFi.
        Serial.println("[BOOT] MINIMAL mode: parked report");
        g_modem.begin();
    }

    esp_ota_img_states_t ota_state;
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running_partition, &ota_state) == ESP_OK &&
        ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        g_ota_boot_validation_pending = true;
        g_ota_boot_validation_start_ms = millis();
        Serial.println("[OTA] New firmware booted; checking runtime before confirmation");
    }
}

void loop() {
    g_power.update();
    g_axp.update();

    // ACTIVE and AWAKE (charging) share the same peripherals; only MINIMAL differs, so only that needs a restart.
    if ((g_power.current_mode() == PowerManager::Mode::MINIMAL) !=
        (g_boot_mode == PowerManager::Mode::MINIMAL)) {
        Serial.println("[MAIN] Power mode changed; restarting");
        delay(100);
        ESP.restart();
    }

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
        case PowerManager::Mode::AWAKE: {
            g_ble.update();
            g_obd.update();
            g_modem.update();

            g_store.set_ble_connected(g_ble.is_connected());
            g_store.set_obd_ready(g_obd.is_ready());
            g_store.set_mqtt_online(g_modem.is_online());

            const uint32_t telemetry_interval =
                g_power.current_mode() == PowerManager::Mode::ACTIVE ? REPORT_INTERVAL_ACTIVE_MS
                                                                     : REPORT_INTERVAL_CHARGING_MS;
            static uint32_t last_telemetry_pub = 0;
            static BmsData last_sent;
            static bool have_last_sent = false;

            BmsData b = g_obd.get_bms_data();
            if (b.valid) {
                g_store.set_bms_data(b);
                uint32_t since = millis() - last_telemetry_pub;
                // Early send on charging start/stop or a SoC step; otherwise only on the interval.
                bool event = have_last_sent && since >= REPORT_EVENT_MIN_GAP_MS &&
                             (b.charging != last_sent.charging ||
                              fabsf(b.soc_display - last_sent.soc_display) >= REPORT_EVENT_SOC_DELTA);
                if (!have_last_sent || since >= telemetry_interval || event) {
                    if (g_modem.publish_telemetry(b)) {
                        last_telemetry_pub = millis();
                        last_sent = b;
                        have_last_sent = true;
                    }
                }
            }

            static uint32_t last_status_pub = 0;
            static bool status_sent = false;
            if (!status_sent || millis() - last_status_pub >= STATUS_INTERVAL_MS) {
                PowerData power;
                power.car_12v = g_power.get_car_12v();
                power.board_v = g_axp.is_initialized() ? g_axp.get_battery_voltage()
                                                       : g_power.get_board_v();
                if (g_modem.publish_power(power)) {
                    g_modem.publish_status(g_store.get_status());
                    last_status_pub = millis();
                    status_sent = true;
                }
            }
            break;
        }

        case PowerManager::Mode::MINIMAL: {
            // Parked: bring LTE up, send one power sample, power the modem down, deep-sleep.
            g_modem.update();

            if (!g_minimal_sent && g_modem.state() == ModemClient::State::READY) {
                PowerData power;
                power.car_12v = g_power.get_car_12v();
                power.board_v = g_axp.is_initialized() ? g_axp.get_battery_voltage()
                                                       : g_power.get_board_v();
                g_minimal_sent = g_modem.publish_power(power);
            }

            bool timed_out = millis() - g_minimal_start_ms > MINIMAL_ONLINE_TIMEOUT_MS;
            if (!g_minimal_closing && ((g_minimal_sent && g_modem.is_idle()) || timed_out)) {
                g_minimal_closing = true;
                g_modem.request_power_off();
            }

            if (g_modem.is_off() && !g_ota_boot_validation_pending) {
                g_axp.enable_modem_power(false);
                if (Serial) {
                    // Monitor attached (bench): deep sleep would drop the USB port, so restart after the same interval.
                    if (millis() - g_minimal_start_ms > MINIMAL_CHECK_INTERVAL_SEC * 1000UL) ESP.restart();
                } else {
                    g_power.enter_sleep(MINIMAL_CHECK_INTERVAL_SEC);
                }
            }
            break;
        }
    }

    if (g_ota_boot_validation_pending &&
        millis() - g_ota_boot_validation_start_ms >= 30000) {
        esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
        if (result == ESP_OK) {
            g_ota_boot_validation_pending = false;
            Serial.println("[OTA] Firmware confirmed; rollback disabled");
        } else {
            Serial.printf("[OTA] Firmware confirmation failed: %s\n", esp_err_to_name(result));
        }
    }

    delay(50);
}
