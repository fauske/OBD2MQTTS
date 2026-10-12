# Project Guidelines

## Architecture
- `src/main.cpp` owns startup and runtime scheduling; implementations live in `src/` and their interfaces and shared types live in `include/`.
- Keep OBD transport and protocol decoding separate. Use `include/i_obd_transport.h` as the transport contract.
- For firmware-specific workflow and hardware constraints, follow the [firmware agent](.github/agents/obd2mqtts-firmware.agent.md).

## Build and Verification
- Build with `pio run -e esp32-s3` (PlatformIO, ESP32-S3, Arduino; see [platformio.ini](platformio.ini)).
- If `pio` is not on the terminal `PATH`, use VS Code's **PlatformIO: Build** command.
- No automated test suite is present. A successful build does not verify hardware behavior.

## Setup and Security
- Follow [README.md](README.md) for setup and [SECURITY.md](SECURITY.md) for secret handling. Use example headers; never commit credentials, certificates, or device-specific secrets.
- Verify behavior in the implementation when docs or comments describe timing or security properties; do not assume those properties are currently enforced.