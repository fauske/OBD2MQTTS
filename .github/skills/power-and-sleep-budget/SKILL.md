---
name: power-and-sleep-budget
description: 'Power, deep sleep and AXP2101 rail management for this device. Use when editing power_manager, axp2101_manager, ignition or charging thresholds, deep sleep, or investigating 12V battery drain.'
---

# Power and sleep budget

- Thresholds live in [include/config.h](../../../include/config.h): ignition on/off voltages, charging detect/release, `DEEP_SLEEP_DURATION_SEC`.
- Debounce is `power_manager.h::IGNITION_DEBOUNCE_MS`; do not redefine it as a macro.
- AXP2101 rails used: DCDC3 (modem, 3.8V) and DCDC1 (3.3V). Disable them before deep sleep and re-enable on wake.
- Parked sampling wakes periodically; keep wake time short (connect, publish, sleep) to protect the car's 12V battery.
- Avoid `delay()`; use `millis()` state machines.
- Changes affect real hardware: do not claim battery or sleep current figures without measurement.
