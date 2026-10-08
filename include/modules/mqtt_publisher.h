#pragma once

#include "../data_types.h"
#include <Arduino.h>

// ============================================================================
// MQTT publisher for telemetry
// ============================================================================
// Handles secure (MQTTS) connections and publishes BMS data.

class MqttPublisher {
public:
    MqttPublisher();

    void  begin();
    void  update();
    bool  is_online() const { return _connected; }

    // Publish telemetry
    bool  publish_telemetry(const BmsData &bms);
    bool  publish_status(const SystemStatus &status);
    bool  publish_location(const GnssData &gnss);

private:
    bool     _connected = false;
    uint32_t _last_reconnect_ms = 0;

    void connect();
};
