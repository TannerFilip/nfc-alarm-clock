# Validation record

Milestone 1, 2026-09-19, ESP-IDF v6.1 commit
`fff9895c82d744c7237be8847347bdd1b07c6643`, Xtensa toolchain supplied by EIM.

| Check | Result |
| --- | --- |
| Production clean isolated build | PASS, application 181600 bytes |
| Development clean isolated build | PASS, application 181696 bytes |
| IDF bootloader and partition size checks | PASS, factory partition 4 MiB |
| Generated ESP32-S3, 32 MB OPI/DTR 80 MHz, octal PSRAM 80 MHz configuration | PASS both profiles |
| Development/simulation settings | OFF production; ON development |
| Production checker given development build | Correctly rejected |
| Wiring CSV vs supplied exported netlist | PASS, 73 terminals agree |
| Supplied hardware file hashes | Unchanged from inspection through final check |
| `git diff --check` | PASS |

Builds emit upstream IDF CMake component dependency/include visibility warnings;
neither application compilation nor linking failed. Initial CMake version-check
failure was fixed before the successful builds. Build logs are preserved in
`build/m1/build-verification.log` and `build/m1-dev/build-verification.log`.

Reproduce configuration/artifact checks after building with the README commands:

```bash
python3 tools/check_build.py build/m1
python3 tools/check_build.py build/m1-dev --development
```

Application SHA-256 values (exact current build artifacts, not a promise of
byte-identical binaries across environment/path changes):

```text
production  2a92c49a673a7d1f9fa18c1e5707335a561b94fa0190e794442c101a93a15846
development 7daf101cfbae252ab4a4ac689be184ad2e86bd6c0886a83352e0129bd284687e
```

Hardware execution: **not performed**. No flash, serial monitor, peripheral test,
audio test or battery-runtime measurement has been performed. No host scheduler
tests exist yet because scheduling is not implemented; those tests are a required
milestone 3 gate, and settings validation tests are a milestone 4 gate.
