#!/usr/bin/env python3
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
MISSION = ROOT / "missions" / "fpga-spi.md"
MANIFEST = Path(__file__).with_name("connectivity_manifest.json")

VALID_DIRECTIONS = {"MPU -> FPGA", "FPGA -> MPU", "Bidirectional"}
VALID_ROLES = {"drive", "sample", "drive_sample"}
SIGNAL_RE = re.compile(r"^[a-z][a-z0-9_]*$")


def normalize_cell(cell):
    cell = cell.strip()
    cell = re.sub(r"`([^`]*)`", r"\1", cell)
    return cell


def parse_assumed_connections(path):
    rows = []
    in_section = False
    in_table = False

    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip() == "Assumed hardware connections:":
            in_section = True
            continue
        if not in_section:
            continue
        if line.startswith("## "):
            break
        if not line.startswith("|"):
            if in_table:
                break
            continue

        cells = [normalize_cell(cell) for cell in line.strip().strip("|").split("|")]
        if cells[0] == "MPU signal/pin":
            in_table = True
            continue
        if cells[0] == "---":
            continue
        if len(cells) != 5:
            raise ValueError(f"bad table row: {line}")

        rows.append(
            {
                "mpu_signal_pin": cells[0],
                "fpga_signal_pin": cells[1],
                "direction": cells[2],
                "voltage_domain": cells[3],
            }
        )

    if not rows:
        raise ValueError(f"no assumed hardware connection rows found in {path}")
    return rows


def key(row):
    return (
        row["mpu_signal_pin"],
        row["fpga_signal_pin"],
        row["direction"],
        row["voltage_domain"],
    )


def expected_roles(direction):
    if direction == "MPU -> FPGA":
        return "drive", "sample"
    if direction == "FPGA -> MPU":
        return "sample", "drive"
    if direction == "Bidirectional":
        return "drive_sample", "drive_sample"
    raise ValueError(f"bad direction: {direction}")


def main():
    expected = parse_assumed_connections(MISSION)
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    jumpers = manifest.get("jumpers")
    if not isinstance(jumpers, list):
        raise ValueError("manifest must contain a jumpers list")

    expected_keys = {key(row) for row in expected}
    manifest_keys = {key(row) for row in jumpers}

    missing = expected_keys - manifest_keys
    extra = manifest_keys - expected_keys
    if missing or extra:
        if missing:
            print("missing manifest rows:", file=sys.stderr)
            for item in sorted(missing):
                print(f"  {item}", file=sys.stderr)
        if extra:
            print("manifest rows not in table:", file=sys.stderr)
            for item in sorted(extra):
                print(f"  {item}", file=sys.stderr)
        return 1

    signals = set()
    for row in jumpers:
        signal = row.get("signal")
        if not isinstance(signal, str) or not SIGNAL_RE.fullmatch(signal):
            raise ValueError(f"bad stable signal name: {signal!r}")
        if signal in signals:
            raise ValueError(f"duplicate signal name: {signal}")
        signals.add(signal)

        direction = row.get("direction")
        if direction not in VALID_DIRECTIONS:
            raise ValueError(f"{signal}: bad direction {direction!r}")

        mpu_role, fpga_role = expected_roles(direction)
        if row.get("mpu_role") != mpu_role or row.get("fpga_role") != fpga_role:
            raise ValueError(
                f"{signal}: expected roles mpu={mpu_role} fpga={fpga_role}, "
                f"got mpu={row.get('mpu_role')} fpga={row.get('fpga_role')}"
            )
        if row.get("mpu_role") not in VALID_ROLES or row.get("fpga_role") not in VALID_ROLES:
            raise ValueError(f"{signal}: invalid drive/sample role")

    print(f"validated {len(jumpers)} gpio connectivity manifest rows")
    return 0


if __name__ == "__main__":
    sys.exit(main())
