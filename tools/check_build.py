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
if errors:
    raise SystemExit("\n".join(errors))
print("PASS: ESP32-S3 memory configuration, build profile and firmware artifacts")
