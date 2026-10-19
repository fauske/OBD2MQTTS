---
name: obd2-ioniq-pids
description: 'OBD2 and Hyundai Ioniq BMS PIDs, ELM327 over BLE, and decoding. Use when editing obd_decoder, ioniq_bms_decoder, ble_transport, adding PIDs, or interpreting SOC, pack voltage/current, aux voltage and charging state.'
---

# OBD2 and Ioniq BMS decoding

## Structure

- Transport: `IObdTransport` ([include/i_obd_transport.h](../../../include/i_obd_transport.h)), implemented by `ble_transport`.
- Decoders must not depend on a specific transport.
- Decoded values go to `BmsData` in [include/data_types.h](../../../include/data_types.h): `soc_bms`, `soc_display`, `pack_v`, `pack_a`, `aux_v`, `charging`.

## Guidance

- Ioniq BMS data is read with manufacturer-specific UDS requests (e.g. service 0x22, PID 0x0101 to the BMS ECU 7E4) and multi-frame ISO-TP responses. Confirm headers and byte offsets against a known reference or a captured log before changing them.
- Document each PID: request, ECU header, byte offsets, scaling, signedness (pack current is signed).
- Validate response length and header before indexing bytes; reject partial frames.
- Keep parsing pure (no I/O) so it can be unit-tested with captured responses.
- Do not claim a decode is correct without a real capture from the car.
