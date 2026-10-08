#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

#include "config.h"
#include "data_store.h"
#include "modules/ble_transport.h"
#include "modules/obd_decoder.h"
#include "modules/mqtt_publisher.h"
#include "ota_manager.h"

DataStore g_store;
BleTransport g_ble;
ObdDecoder g_obd(g_ble);
MqttPublisher g_mqtt;
OtaManager g_ota;

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);

    Serial.println("\n=== OBD2MQTTS boot ===");

    // Start modules
    g_ble.begin();
    g_obd.begin();
    g_mqtt.begin();

    if (ENABLE_OTA) {
        g_ota.begin(DEVICE_HOSTNAME, OTA_PASSWORD, OTA_PORT);
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[WiFi] Connecting to ");
    Serial.println(WIFI_SSID);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.printf("[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
}

void loop() {
    if (ENABLE_OTA && g_ota.is_ready()) {
        g_ota.handle();
    }

    g_ble.update();
    g_obd.update();
    g_mqtt.update();

    g_store.set_ble_connected(g_ble.is_connected());
    g_store.set_obd_ready(g_obd.is_ready());
    g_store.set_mqtt_online(g_mqtt.is_online());

    static uint32_t last_pub = 0;
    if (millis() - last_pub >= REPORT_INTERVAL_MS) {
        last_pub = millis();

        BmsData current = g_obd.get_bms_data();
        if (current.valid) {
            g_store.set_bms_data(current);
            g_mqtt.publish_telemetry(current);
        }

        SystemStatus status = g_store.get_status();
        g_mqtt.publish_status(status);
    }

    delay(50);
}
