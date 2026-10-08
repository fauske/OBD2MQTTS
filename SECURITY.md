# Security Policy

## Reporting a security issue

If you discover a security issue in this project, please do not open a public issue.
Instead, contact the maintainer privately and report the vulnerability.

## Secret handling policy

This repository is public. That means:

- never commit real Wi-Fi credentials
- never commit MQTT credentials
- never commit broker certificates or private keys
- never commit OTA passwords
- never commit `include/config.h` or `include/secrets.h`

Use the public example files instead:

- `include/config.h.example`
- `include/secrets.h.example`

and keep the real values in local untracked files.

## Production guidance

- Use MQTTS (TLS) for all cloud communication
- Prefer certificate verification with a trusted CA
- Use a strong, unique OTA password per device
- Keep local secret files outside the repository
- Review `git status` before every push
