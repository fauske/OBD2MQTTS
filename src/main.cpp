#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

#include "config.h"
#include "data_store.h"
#include "include/modules/ble_transport.h"
#include "include/modules/obd_decoder.h"
#include "include/modules/mqtt_publisher.h"
#include "ota_manager.h"

DataStore store;
BleTransport ble;
ObdDecoder obd(ble);
MqttPublisher mqtt;
OtaManager ota;

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);

    Serial.println("\n=== OBD2MQTTS boot ===");

    // Start modules
    ble.begin();
    obd.begin();
    mqtt.begin();

    if (ENABLE_OTA) {
        ota.begin(DEVICE_HOSTNAME, OTA_PASSWORD, OTA_PORT);
    }

    // WiFi should be connected before MQTT is available
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
    // OTA first
    if (ENABLE_OTA && ota.is_ready()) {
        ota.handle();
    }

    // Pump modules
    ble.update();
    obd.update();
    mqtt.update();

    // Mirror status bits
    store.set_ble_connected(ble.is_connected());
    store.set_obd_ready(obd.is_ready());
    store.set_mqtt_online(mqtt.is_online());

    // Simple status sample
    static uint32_t last_pub = 0;
    if (millis() - last_pub > REPORT_INTERVAL_MS) {
        last_pub = millis();

        BmsData current = store.get_bms_data();
        if (current.valid) {
            mqtt.publish_telemetry(current);
        }

        SystemStatus status = store.get_status();
        mqtt.publish_status(status);
    }

    delay(50);
}
