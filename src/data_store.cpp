#include "data_store.h"

DataStore::DataStore() {
    _mux = portMUX_INITIALIZER_UNLOCKED;
}

void DataStore::set_bms_data(const BmsData &d) {
    taskENTER_CRITICAL(&_mux);
    _bms = d;
    taskEXIT_CRITICAL(&_mux);
}

BmsData DataStore::get_bms_data() const {
    BmsData out;
    taskENTER_CRITICAL(&_mux);
    out = _bms;
    taskEXIT_CRITICAL(&_mux);
    return out;
}

bool DataStore::is_bms_fresh() const {
    BmsData d = get_bms_data();
    if (!d.valid) return false;
    return (millis() - d.last_update_ms) < 15000;
}

void DataStore::set_power_data(const PowerData &p) {
    taskENTER_CRITICAL(&_mux);
    _power = p;
    taskEXIT_CRITICAL(&_mux);
}

PowerData DataStore::get_power_data() const {
    PowerData out;
    taskENTER_CRITICAL(&_mux);
    out = _power;
    taskEXIT_CRITICAL(&_mux);
    return out;
}

void DataStore::set_gnss_data(const GnssData &g) {
    taskENTER_CRITICAL(&_mux);
    _gnss = g;
    taskEXIT_CRITICAL(&_mux);
}

GnssData DataStore::get_gnss_data() const {
    GnssData out;
    taskENTER_CRITICAL(&_mux);
    out = _gnss;
    taskEXIT_CRITICAL(&_mux);
    return out;
}

void DataStore::set_trip_data(const TripData &t) {
    taskENTER_CRITICAL(&_mux);
    _trip = t;
    taskEXIT_CRITICAL(&_mux);
}

TripData DataStore::get_trip_data() const {
    TripData out;
    taskENTER_CRITICAL(&_mux);
    out = _trip;
    taskEXIT_CRITICAL(&_mux);
    return out;
}

void DataStore::set_ble_connected(bool v) {
    taskENTER_CRITICAL(&_mux);
    _status.ble_connected = v;
    taskEXIT_CRITICAL(&_mux);
}

void DataStore::set_obd_ready(bool v) {
    taskENTER_CRITICAL(&_mux);
    _status.obd_ready = v;
    taskEXIT_CRITICAL(&_mux);
}

void DataStore::set_modem_ready(bool v) {
    taskENTER_CRITICAL(&_mux);
    _status.modem_ready = v;
    taskEXIT_CRITICAL(&_mux);
}

void DataStore::set_mqtt_online(bool v) {
    taskENTER_CRITICAL(&_mux);
    _status.mqtt_online = v;
    taskEXIT_CRITICAL(&_mux);
}

void DataStore::set_rssi(int8_t rssi) {
    taskENTER_CRITICAL(&_mux);
    _status.cell_rssi = rssi;
    taskEXIT_CRITICAL(&_mux);
}

SystemStatus DataStore::get_status() const {
    SystemStatus out;
    taskENTER_CRITICAL(&_mux);
    out = _status;
    taskEXIT_CRITICAL(&_mux);
    return out;
}
