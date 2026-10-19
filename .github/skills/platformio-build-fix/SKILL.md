---
name: platformio-build-fix
description: 'Build and fix PlatformIO errors for this ESP32-S3 project. Use when running pio run, platformio not found, UnknownPackageError, library not found, include path errors, compile or link errors.'
---

# PlatformIO build and fix

## Run the build

- `platformio` is usually not on PATH. Use `~/.platformio/penv/bin/platformio run`.
- Filter output: `... run 2>&1 | grep -iE "error|fail|undefined|not found|SUCCESS"`.
- Never upload (`--target upload`) without an explicit request.

## Known pitfalls

- Library owner names are exact: `lewisxhe/XPowersLib` (not `lewishe`), `h2zero/NimBLE-Arduino`.
- Includes: `-Iinclude` is set, so use `#include "config.h"` and `"modules/x.h"`. Do not use `../../include/...`.
- `config.h` macros must not share names with `static constexpr` members (e.g. `IGNITION_DEBOUNCE_MS`).
- Methods that take `portMUX_TYPE` locks while `const` need `mutable portMUX_TYPE`.
- XPowersLib: include `XPowersLib.h`, class `XPowersAXP2101`. Many power-channel methods are protected in 0.2.x; check the installed header in `.pio/libdeps/*/XPowersLib/src` before calling.
- NimBLE-Arduino is pinned to 1.4.x; the callback API differs in 2.x. See the `library-api-compat` skill.
- Missing `MQTT_*` macros: `include/secrets.h` must be copied from `secrets.h.example` (it is untracked).

## Workflow

1. Run the build, read the first error per file, fix root causes before warnings.
2. Check installed library headers in `.pio/libdeps/` instead of guessing APIs.
3. Rebuild until `[SUCCESS]`. A successful compile is not hardware validation.
