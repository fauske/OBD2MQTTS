#pragma once

#include <Arduino.h>

class OtaManager {
public:
    void begin(const char *hostname, const char *password, uint16_t port = 3232);
    void handle();
    bool is_ready() const;

private:
    bool _ready = false;
};
