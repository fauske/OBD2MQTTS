---
name: mqtt-tls-security
description: 'Security, secrets and TLS certificates for MQTTS and OTA. Use when handling certificates, CA validation, setInsecure, secrets.h, OTA password, credentials, or reviewing the project for security issues.'
---

# MQTT TLS, certificates and secrets

## Secrets

- Real values live only in untracked `include/secrets.h` (copy from [include/secrets.h.example](../../../include/secrets.h.example)).
- Never put credentials, broker hosts or certificates in tracked files, logs, commit messages or replies.
- Check `.gitignore` covers `include/secrets.h` before committing.

## TLS

- Current code in [src/modules/modem_client.cpp](../../../src/modules/modem_client.cpp) calls `setInsecure()`, which disables certificate validation (MITM risk). Treat as a known gap.
- Preferred: `setCACert(MQTT_CA_CERT)` with the broker's CA in PEM, port 8883, and a correct system time if expiry checks are needed.
- Certificate pinning/rotation: note the CA expiry date and plan an OTA update path before it expires.
- On the SIM7080 path, load the CA into the modem (`AT+CSSLCFG`/`AT+CASSLCFG`) rather than relying on ESP32 TLS.

## OTA

- Keep `OTA_PASSWORD` strong and per-device; do not weaken OTA protections without an explicit request.

## Review checklist

- No secrets in git history (`git log -p -S<value>`); rotate any that leaked.
- TLS validation enabled, minimal MQTT ACLs on the broker, unique client IDs.
- Payloads contain no personal data beyond what is needed.
