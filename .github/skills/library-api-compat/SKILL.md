---
name: library-api-compat
description: 'Library version and API compatibility for NimBLE-Arduino, XPowersLib, PubSubClient, ArduinoJson. Use when a compile error mentions override, protected, no matching function, or after changing lib_deps versions.'
---

# Library API compatibility

Always verify against the installed headers in `.pio/libdeps/esp32-s3/<lib>/` before editing calls.

## NimBLE-Arduino

- Pinned `^1.4.3` in `platformio.ini`. 1.x uses `NimBLEAdvertisedDeviceCallbacks`, `setAdvertisedDeviceCallbacks(cb, bool)` and `registerForNotify`-style callbacks with a function pointer.
- 2.x changed callback classes and made `~NimBLEClient()` private (use `NimBLEDevice::deleteClient`). Do not bump to 2.x without porting [src/modules/ble_transport.cpp](../../../src/modules/ble_transport.cpp).

## XPowersLib 0.2.x

- Header `XPowersLib.h`, class `XPowersAXP2101`, build flag `-DXPOWERS_CHIP_AXP2101`.
- Some channel/charge methods are protected or named differently (`setPowerKeyPressOnTime`, `disableGauge`); read `XPowersAXP2101.tpp` first.

## ArduinoJson 7

- `DynamicJsonDocument` is deprecated; prefer `JsonDocument`.

## PubSubClient

- Default buffer is small; this project calls `setBufferSize(512)`. Keep payloads below it.
