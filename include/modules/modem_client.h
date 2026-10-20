#pragma once

#include "../data_types.h"
#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

class ModemClient {
public:
    enum class State : uint8_t {
        IDLE,
        INITIALIZING,
        WAITING_NETWORK,
        CONNECTING_DATA,
        CONNECTING_MQTT,
        READY,
        ERROR
    };

    ModemClient();

    void begin();
    void update();
    bool is_online() { return _mqtt_client.connected(); }
    bool is_busy() const { return _tx_in_flight; }
    int mqtt_state();
    bool is_network_registered() const { return _network_registered; }
    bool is_data_connected() const { return _data_connected; }
    int signal_quality() const { return _signal_quality; }
    void set_gnss_enabled(bool enabled) { _gnss_requested = enabled; }
    bool gnss_ready() const { return _gnss_enabled; }
    bool read_gnss(GnssData &data);

    bool publish_telemetry(const BmsData &bms);
    bool publish_gnss(const GnssData &gnss);
    bool publish_trip(const TripData &trip);
    bool publish_power(const PowerData &power);
    bool publish_status(const SystemStatus &status);

    State state() const { return _state; }

private:
    State _state = State::IDLE;

    TinyGsm _modem;
    TinyGsmClientSecure _secure_client;
    PubSubClient _mqtt_client;

    bool _tx_in_flight = false;
    bool _network_registered = false;
    bool _data_connected = false;
    bool _gnss_requested = false;
    bool _gnss_enabled = false;
    bool _gnss_has_fix = false;
    bool _ca_ready = false;
    bool _time_synced = false;
    uint8_t _power_key_attempts = 0;
    uint32_t _last_reconnect_ms = 0;
    uint32_t _last_status_log_ms = 0;
    uint32_t _last_network_check_ms = 0;
    uint32_t _last_data_attempt_ms = 0;
    uint32_t _last_mqtt_attempt_ms = 0;
    uint32_t _last_time_sync_ms = 0;
    uint32_t _last_signal_check_ms = 0;
    uint32_t _gnss_enabled_ms = 0;
    uint32_t _gnss_ttff_ms = 0;
    uint32_t _last_gnss_command_ms = 0;
    char _ca_certificate_name[24] = {};

    int _signal_quality = 0;

    void do_init();
    void do_wait_network();
    void do_connect_data();
    void do_connect_mqtt();
    void update_gnss_state();
    bool connect_mqtt();
    bool install_ca_certificate();
    void report_error(const char *message);
};
