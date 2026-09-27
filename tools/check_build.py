"""Check a generated firmware build configuration; does not exercise hardware."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("build", type=Path)
parser.add_argument("--development", action="store_true")
args = parser.parse_args()
config = {}
for line in (args.build / "sdkconfig").read_text().splitlines():
    if line.startswith("CONFIG_") and "=" in line:
        key, value = line.split("=", 1)
        config[key] = value

expected = {
    "IDF_TARGET": '"esp32s3"',
    "ESPTOOLPY_OCT_FLASH": "y",
    "ESPTOOLPY_FLASHMODE_OPI": "y",
    "ESPTOOLPY_FLASH_SAMPLE_MODE_DTR": "y",
    "ESPTOOLPY_FLASHSIZE": '"32MB"',
    "ESPTOOLPY_FLASHFREQ": '"80m"',
    "SPIRAM": "y",
    "SPIRAM_MODE_OCT": "y",
    "SPIRAM_SPEED": "80",
    "SPIRAM_MEMTEST": "y",
    "PARTITION_TABLE_CUSTOM": "y",
    "LOG_DYNAMIC_LEVEL_CONTROL": "y",
}
errors = [f"{key}: expected {value}, got {config.get('CONFIG_' + key)}"
          for key, value in expected.items() if config.get("CONFIG_" + key) != value]
for key in ("CLOCK_DEVELOPMENT_BUILD", "CLOCK_SIMULATED_NFC"):
    if (config.get("CONFIG_" + key) == "y") != args.development:
        errors.append(f"{key}: profile mismatch")
for name in ("nfc-alarm-clock.bin", "nfc-alarm-clock.elf",
             "bootloader/bootloader.bin", "partition_table/partition-table.bin"):
    path = args.build / name
    if not path.is_file() or path.stat().st_size == 0:
        errors.append(f"Missing/empty artifact: {name}")
elf = args.build / "nfc-alarm-clock.elf"
if elf.is_file():
    simulator_markers = (b"nfc_sim", b"DEV SIM NFC - NVS")
    artifact = elf.read_bytes()
    for marker in simulator_markers:
        if (marker in artifact) != args.development:
            errors.append(f"Simulator artifact gate mismatch for {marker!r}")
    for marker in (b"NVS SETTINGS + JOURNAL", b"WIFI:T:WPA", b"SETUP TEXT - B1 FOR QR"):
        if marker not in artifact:
            errors.append(f"Missing Milestone 4 artifact marker {marker!r}")
    for marker in (b"milestone 5 / 0.5.0", b"PN7160 NCI driver", b"physical NFC-A UID=", b"tag_enroll"):
        if marker not in artifact:
            errors.append(f"Missing Milestone 5 artifact marker {marker!r}")
    for marker in (b"/api/login", b"setup_session=", b"Setup secret"):
        if marker in artifact:
            errors.append(f"Removed setup-code artifact remains: {marker!r}")
    production_block = b"active journal blocked and silenced"
    if (production_block in artifact) == args.development:
        errors.append("Production active-journal dismissal gate mismatch")
if errors:
    raise SystemExit("\n".join(errors))
print("PASS: ESP32-S3 memory configuration, build profile and firmware artifacts")
