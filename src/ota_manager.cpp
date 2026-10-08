#include "ota_manager.h"
#include <ArduinoOTA.h>

void OtaManager::begin(const char *hostname, const char *password, uint16_t port) {
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setPassword(password);
    ArduinoOTA.setPort(port);

    ArduinoOTA.onStart([]() {
        Serial.println("[OTA] Update started");
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("[OTA] Update finished");
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error[%u]\n", error);
    });

    ArduinoOTA.begin();
    _ready = true;
}

void OtaManager::handle() {
    if (_ready) {
        ArduinoOTA.handle();
    }
}

bool OtaManager::is_ready() const {
    return _ready;
}
