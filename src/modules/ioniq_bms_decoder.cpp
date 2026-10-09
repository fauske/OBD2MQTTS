#include "../../include/modules/ioniq_bms_decoder.h"
#include <string.h>
#include <ctype.h>

// ============================================================================
// Hyundai Ioniq BMS Decoder Implementation
// ============================================================================
// Protocol reference:
//   PID 2101: Battery State (40 bytes typical)
//   PID 2105: Battery Thermal & Health (variable length)
//
// Format from ELM327:
//   Response: "62 01 <40 bytes of data> >"
//   where "62" = positive response, "01" = PID number

bool IoniqBmsDecoder::parse_hex_response(const char *response,
                                         uint8_t *out, size_t &out_len) {
    out_len = 0;
    if (!response) return false;

    const char *p = response;
    while (*p && out_len < 256) {
        // Skip whitespace
        while (*p && isspace(*p)) p++;
        if (!*p || *p == '>') break;

        // Parse two hex digits
        if (!isxdigit(p[0]) || !isxdigit(p[1])) {
            return false;
        }

        uint8_t byte = 0;
        for (int i = 0; i < 2; i++) {
            byte = byte * 16 + (isdigit(*p) ? (*p - '0') : (tolower(*p) - 'a' + 10));
            p++;
        }
        out[out_len++] = byte;
    }
    return out_len > 0;
}

float IoniqBmsDecoder::scale_voltage_raw(uint16_t raw) {
    // Pack voltage: 0.0625 V per LSB (1/16 V)
    // Range: 0 - 409.6 V
    return (raw * 0.0625f);
}

float IoniqBmsDecoder::scale_current_raw(int16_t raw) {
    // Pack current: 0.1 A per LSB (discharge +, charge -)
    // Range: -3276.8 A to +3276.7 A (typically ±200 A)
    return (raw * 0.1f);
}

float IoniqBmsDecoder::scale_soc_raw(uint8_t raw) {
    // SoC: 0.5% per LSB
    // Range: 0 - 100%
    return (raw * 0.5f);
}

float IoniqBmsDecoder::scale_temp_raw(int8_t raw) {
    // Temperature: 1°C per LSB, offset -40°C
    // Range: -40°C to +85°C
    return (raw - 40);
}

bool IoniqBmsDecoder::decode_2101(const uint8_t *data, size_t len,
                                  float &soc_bms, float &soc_display,
                                  float &pack_v, float &pack_a,
                                  uint8_t &cell_max_no, uint8_t &cell_min_no,
                                  float &cell_v_max, float &cell_v_min) {
    // PID 2101 structure (Ioniq BMS):
    // Byte 0-1: BMS SoC (raw value, 0.5% per LSB)
    // Byte 2-3: Display SoC (what dash shows)
    // Byte 4-5: Pack voltage (0.0625 V per LSB, big-endian)
    // Byte 6-7: Pack current (0.1 A per LSB, big-endian, 2's complement)
    // Byte 8: Max cell voltage cell number (1-96)
    // Byte 9: Min cell voltage cell number (1-96)
    // Byte 10-11: Max cell voltage (raw, ~4 mV per LSB)
    // Byte 12-13: Min cell voltage (raw, ~4 mV per LSB)
    // Byte 14+: Additional cell data, coolant temp, etc.

    if (len < 14) {
        Serial.printf("[BMS] 2101 too short: %u bytes\n", len);
        return false;
    }

    // Bytes 0-1: BMS SoC
    uint16_t soc_bms_raw = (data[0] << 8) | data[1];
    soc_bms = scale_soc_raw((uint8_t)(soc_bms_raw >> 8));  // Upper byte is percentage

    // Bytes 2-3: Display SoC
    uint16_t soc_disp_raw = (data[2] << 8) | data[3];
    soc_display = scale_soc_raw((uint8_t)(soc_disp_raw >> 8));

    // Bytes 4-5: Pack voltage (big-endian)
    uint16_t pack_v_raw = (data[4] << 8) | data[5];
    pack_v = scale_voltage_raw(pack_v_raw);

    // Bytes 6-7: Pack current (big-endian, signed)
    int16_t pack_a_raw = (data[6] << 8) | data[7];
    pack_a = scale_current_raw(pack_a_raw);

    // Byte 8: Max cell number
    cell_max_no = data[8];

    // Byte 9: Min cell number
    cell_min_no = data[9];

    // Bytes 10-11: Max cell voltage (raw, ~4mV per bit)
    uint16_t cell_v_max_raw = (data[10] << 8) | data[11];
    cell_v_max = (cell_v_max_raw * 0.004f);  // 4mV per LSB

    // Bytes 12-13: Min cell voltage (raw, ~4mV per bit)
    uint16_t cell_v_min_raw = (data[12] << 8) | data[13];
    cell_v_min = (cell_v_min_raw * 0.004f);

    Serial.printf("[BMS] 2101 decoded: SoC=%.1f%% PkV=%.1fV PkA=%.1fA CellMax=%.3fV CellMin=%.3fV\n",
                  soc_bms, pack_v, pack_a, cell_v_max, cell_v_min);

    return true;
}

bool IoniqBmsDecoder::decode_2105(const uint8_t *data, size_t len,
                                  float &temp_max, float &temp_min,
                                  float &temp_inlet,
                                  float &soh, float &avail_chg_kw, float &avail_dis_kw) {
    // PID 2105 structure (Ioniq BMS thermal and health):
    // Byte 0: Max temperature (offset -40°C)
    // Byte 1: Min temperature (offset -40°C)
    // Byte 2: Coolant inlet temp (offset -40°C)
    // Byte 3: State of Health (%)
    // Byte 4-5: Available charge power (kW, big-endian)
    // Byte 6-7: Available discharge power (kW, big-endian)
    // Byte 8+: Additional module temps, fan speed, etc.

    if (len < 8) {
        Serial.printf("[BMS] 2105 too short: %u bytes\n", len);
        return false;
    }

    // Byte 0: Max temperature
    temp_max = scale_temp_raw((int8_t)data[0]);

    // Byte 1: Min temperature
    temp_min = scale_temp_raw((int8_t)data[1]);

    // Byte 2: Coolant inlet temperature
    temp_inlet = scale_temp_raw((int8_t)data[2]);

    // Byte 3: State of Health
    soh = (float)data[3];  // Direct percentage

    // Bytes 4-5: Available charge power (kW, big-endian)
    uint16_t chg_kw_raw = (data[4] << 8) | data[5];
    avail_chg_kw = (chg_kw_raw * 0.1f);  // 0.1 kW per LSB

    // Bytes 6-7: Available discharge power (kW, big-endian)
    uint16_t dis_kw_raw = (data[6] << 8) | data[7];
    avail_dis_kw = (dis_kw_raw * 0.1f);

    Serial.printf("[BMS] 2105 decoded: TMax=%.0f°C TMin=%.0f°C Tinlet=%.0f°C SoH=%.0f%% ChgKW=%.1f DisKW=%.1f\n",
                  temp_max, temp_min, temp_inlet, soh, avail_chg_kw, avail_dis_kw);

    return true;
}
