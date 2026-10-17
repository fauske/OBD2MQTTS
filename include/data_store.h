#pragma once

#include <Arduino.h>
#include "data_types.h"

class DataStore {
public:
    DataStore();

    void set_bms_data(const BmsData &d);
    BmsData get_bms_data() const;
    bool is_bms_fresh() const;

    void set_power_data(const PowerData &p);
    PowerData get_power_data() const;

    void set_gnss_data(const GnssData &g);
    GnssData get_gnss_data() const;

    void set_trip_data(const TripData &t);
    TripData get_trip_data() const;

    void set_ble_connected(bool v);
    void set_obd_ready(bool v);
    void set_modem_ready(bool v);
    void set_mqtt_online(bool v);
    void set_rssi(int8_t rssi);

    SystemStatus get_status() const;

private:
    BmsData _bms;
    PowerData _power;
    GnssData _gnss;
    TripData _trip;
    SystemStatus _status;
    mutable portMUX_TYPE _mux;
};
