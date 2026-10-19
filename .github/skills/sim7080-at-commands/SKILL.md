---
name: sim7080-at-commands
description: 'SIM7080G modem AT-command work: LTE-M/NB-IoT attach, PDP context, MQTT(S) over AT. Use when editing modem_client, adding AT commands, parsing modem responses, or debugging cellular connectivity on the LilyGo T-SIM7080G-S3.'
---

# SIM7080 AT commands

Verify command syntax against the SIMCom SIM7080 AT command and MQTT application notes; do not guess parameters.

## Typical flow

1. Power/enable modem, wait for `AT` -> `OK`.
2. `AT+CPIN?`, `AT+CSQ`, `AT+CGATT?`, `AT+CEREG?` until registered.
3. Set APN and activate PDP: `AT+CNCFG`, `AT+CNACT`.
4. Configure MQTT: `AT+SMCONF="URL"`, `"CLIENTID"`, `"USERNAME"`, `"PASSWORD"`, `"KEEPTIME"`.
5. For TLS: configure CA via `AT+CSSLCFG` / `AT+CASSLCFG`, then connect with `AT+SMCONN`.
6. Publish with `AT+SMPUB`; check `AT+SMSTATE?`.

## Project rules

- Non-blocking: drive steps from `update()` with `millis()` timeouts, never `delay()`.
- Send/receive through `send_at_command` and `read_response` in [src/modules/modem_client.cpp](../../../src/modules/modem_client.cpp); use bounded buffers and timeouts.
- Never log credentials from `AT+SMCONF` lines.
- See `mqtt-tls-security` for certificate handling.
