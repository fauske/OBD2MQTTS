#pragma once

#include <Arduino.h>
#include <cmath>

// ============================================================================
// Data types for BMS telemetry
// ============================================================================
// Plain structs with no methods. Modules exchange ONLY these via DataStore.
// All float values default to NAN (not a number) to indicate missing data.

// BMS (Battery Management System) snapshot from OBD2 PID 2101/2105
struct BmsData {
    // --- State of charge ---
    float   soc_bms       = NAN;   // % (raw BMS SoC)
    float   soc_display   = NAN;   // % (dashboard SoC)

    // --- Pack electrical ---
    float   pack_v        = NAN;   // V (pack voltage)
    float   pack_a        = NAN;   // A (pack current, + discharge, - charge)
    float   aux_v         = NAN;   // V (12V auxiliary battery)

    // --- Cells ---
    float   cell_v_max    = NAN;   // V (maximum cell voltage)
    float   cell_v_min    = NAN;   // V (minimum cell voltage)
    uint8_t cell_max_no   = 0;     // cell number with max voltage
    uint8_t cell_min_no   = 0;     // cell number with min voltage

    // --- Temperatures ---
    float   temp_max      = NAN;   // °C (maximum temperature)
    float   temp_min      = NAN;   // °C (minimum temperature)
    float   temp_inlet    = NAN;   // °C (coolant inlet temperature)

    // --- Per-module temperatures ---
    static const uint8_t MAX_MODULE_TEMPS = 5;
    int8_t  module_temps[MAX_MODULE_TEMPS] = {};  // °C per module
    uint8_t module_temp_count = 0;                 // how many decoded

    // --- BMS power limits ---
    float   avail_chg_kw  = NAN;   // kW max charge power
    float   avail_dis_kw  = NAN;   // kW max discharge power

    // --- Cooling ---
    uint8_t fan_status    = 0;     // fan commanded step
    uint8_t fan_speed     = 0;     // fan actual speed %

    // --- Charging state (derived from pack_a) ---
    bool    charging      = false;
    bool    dc_charging   = false; // true if DC (fast) charging

    // --- Lifetime counters (monotonic) ---
    float   cum_charge_ah    = NAN;   // Ah into pack, lifetime
    float   cum_discharge_ah = NAN;   // Ah out of pack, lifetime
    float   cum_charge_kwh   = NAN;   // kWh into pack, lifetime
    float   cum_discharge_kwh = NAN;  // kWh out of pack, lifetime
    uint32_t operating_time_s = 0;    // BMS powered-on seconds, lifetime

    // --- Health ---
    float   soh           = NAN;   // % state of health

    // --- Bookkeeping ---
    uint32_t last_update_ms = 0;   // millis() when last decoded
    bool     valid          = false; // data is valid and fresh
};

// Rail voltages (board battery + car 12V)
struct PowerData {
    float    board_v       = NAN;   // V (ESP32 board LiPo)
    float    car_12v       = NAN;   // V (vehicle 12V battery)
    uint32_t last_update_ms = 0;
};

// GNSS position (captured once per trip at park)
struct GnssData {
    float    lat           = NAN;   // degrees, + = north
    float    lon           = NAN;   // degrees, + = east
    float    alt_m         = NAN;   // meters above sea level
    float    hdop          = NAN;   // horizontal dilution of precision
    uint8_t  sats_view     = 0;     // satellites visible
    uint32_t ttff_ms       = 0;     // time to first fix, ms
    char     utc[21]       = {};    // "YYYY-MM-DDTHH:MM:SSZ"
    bool     valid         = false; // solution meets quality gates
    uint32_t last_update_ms = 0;
};

// Trip facts (captured once per trip at ignition-off)
struct TripData {
    uint32_t odometer_km   = 0;
    bool     odo_valid     = false;

    static const uint8_t MAX_DTC = 8;
    char     dtc[MAX_DTC][6] = {};  // "P1A2B" + NUL
    uint8_t  dtc_count     = 0;
    bool     dtc_valid     = false; // true = query completed (even if 0 codes)
};

// System status (health indicators)
struct SystemStatus {
    bool     ble_connected  = false;
    bool     obd_ready      = false; // ELM327 initialized and responding
    bool     modem_ready    = false;
    bool     mqtt_online    = false;
    int8_t   cell_rssi      = 0;     // dBm (0 = unknown)
    uint32_t uptime_s       = 0;
};
