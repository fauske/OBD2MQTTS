#pragma once

#ifndef ENABLE_SERIAL_DIAGNOSTICS
#define ENABLE_SERIAL_DIAGNOSTICS 1
#endif

#if ENABLE_SERIAL_DIAGNOSTICS
#define SERIAL_DIAG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define SERIAL_DIAG_PRINTF(...) ((void)0)
#endif
