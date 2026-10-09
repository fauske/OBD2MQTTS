#pragma once

#include "../i_obd_transport.h"
#include "../data_types.h"
#include <Arduino.h>

// ============================================================================
// OBD2 decoder for ELM327 + Hyundai Ioniq BMS
// ============================================================================
// Handles ELM327 initialization, ISO-TP multi-frame reassembly, and
// Ioniq-specific BMS data decoding from PIDs 2101/2105.

class ObdDecoder {
public:
    ObdDecoder(IObdTransport &transport);

    void  begin();
    void  update();
    bool  is_ready() const { return _elm_ready; }
    bool  have_fresh_data() const;

    BmsData get_bms_data() const { return _bms_data; }
    void    request_trip_queries();

private:
    enum class InitState : uint8_t {
        IDLE,
        RESET,
        ATE0,
        ATL0,
        ATSP6,
        READY
    };

    enum class PollState : uint8_t {
        IDLE,
        REQUESTING_2101,
        WAITING_2101,
        REQUESTING_2105,
        WAITING_2105,
        DONE
    };

    IObdTransport &_transport;
    InitState _init_state = InitState::IDLE;
    PollState _poll_state = PollState::IDLE;

    bool     _elm_ready  = false;
    BmsData  _bms_data;

    uint32_t _last_poll_ms = 0;
    uint32_t _cmd_sent_ms  = 0;
    uint32_t _init_started_ms = 0;

    void do_init();
    void do_poll();
    void process_response();
    bool decode_2101(const uint8_t *data, size_t len);
    bool decode_2105(const uint8_t *data, size_t len);
    bool parse_hex_response(const char *response, uint8_t *out, size_t &out_len);
    float decode_voltage(uint16_t raw);
    float decode_current(int16_t raw);
    float decode_soc(uint8_t raw);
};
