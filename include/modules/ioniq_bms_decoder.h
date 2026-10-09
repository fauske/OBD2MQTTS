#pragma once

#include <Arduino.h>
#include <cstdint>

// ============================================================================
// Hyundai Ioniq BMS Protocol (OBD2 PIDs 2101 and 2105)
// ============================================================================
// Reference: Ioniq 2016-2022 (both PHEV and BEV)
// Communication: ELM327 -> CAN ID 0x7DF -> BMS ECU 0x7E8
// Responses: 0x62 (PID response marker)

class IoniqBmsDecoder {
public:
    // Decode PID 2101 response (main BMS parameters)
    static bool decode_2101(const uint8_t *data, size_t len,
                           float &soc_bms, float &soc_display,
                           float &pack_v, float &pack_a,
                           uint8_t &cell_max_no, uint8_t &cell_min_no,
                           float &cell_v_max, float &cell_v_min);

    // Decode PID 2105 response (temperature and health)
    static bool decode_2105(const uint8_t *data, size_t len,
                           float &temp_max, float &temp_min,
                           float &temp_inlet,
                           float &soh, float &avail_chg_kw, float &avail_dis_kw);

    // Helper: convert hex response string to binary
    static bool parse_hex_response(const char *response,
                                  uint8_t *out, size_t &out_len);

private:
    // Scaling helpers
    static float scale_voltage_raw(uint16_t raw);
    static float scale_current_raw(int16_t raw);
    static float scale_soc_raw(uint8_t raw);
    static float scale_temp_raw(int8_t raw);
};
