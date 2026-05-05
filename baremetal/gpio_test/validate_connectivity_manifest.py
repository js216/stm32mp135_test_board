#!/usr/bin/env python3
import json
import re
import sys
from pathlib import Path

import dry_run_connectivity
import generate_connectivity_scripts


ROOT = Path(__file__).resolve().parents[3]
MISSION = ROOT / "missions" / "fpga-spi.md"
MANIFEST = Path(__file__).with_name("connectivity_manifest.json")

VALID_DIRECTIONS = {"MPU -> FPGA", "FPGA -> MPU", "Bidirectional"}
VALID_ROLES = {"drive", "sample", "drive_sample"}
VALID_CONTROLLERS = {"mpu", "fpga"}
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


def role_allows_drive(role):
    return role in {"drive", "drive_sample"}


def role_allows_sample(role):
    return role in {"sample", "drive_sample"}


def controller_role(row, controller):
    return row[f"{controller}_role"]


def allowed_driver_sampler_pairs(row):
    pairs = set()
    for driver in sorted(VALID_CONTROLLERS):
        for sampler in sorted(VALID_CONTROLLERS - {driver}):
            if role_allows_drive(controller_role(row, driver)) and role_allows_sample(
                controller_role(row, sampler)
            ):
                pairs.add((driver, sampler))
    return pairs


def require_bit(value, field, signal):
    if isinstance(value, bool) or value not in {0, 1}:
        raise ValueError(f"{signal}: vector {field} must be 0 or 1, got {value!r}")
    return value


def validate_first_pass_test_plan(manifest, jumpers_by_signal):
    test_plan = manifest.get("first_pass_test_plan")
    if not isinstance(test_plan, list):
        raise ValueError("manifest must contain a first_pass_test_plan list")

    seen_vectors = {signal: set() for signal in jumpers_by_signal}
    for index, vector in enumerate(test_plan):
        if not isinstance(vector, dict):
            raise ValueError(f"test vector {index}: must be an object")

        signal = vector.get("signal")
        if signal not in jumpers_by_signal:
            raise ValueError(f"test vector {index}: unknown signal {signal!r}")

        driver = vector.get("driver")
        sampler = vector.get("sampler")
        if driver not in VALID_CONTROLLERS or sampler not in VALID_CONTROLLERS:
            raise ValueError(f"{signal}: invalid controller in vector {index}")
        if driver == sampler:
            raise ValueError(f"{signal}: vector {index} uses same driver and sampler")

        allowed_pairs = allowed_driver_sampler_pairs(jumpers_by_signal[signal])
        if (driver, sampler) not in allowed_pairs:
            raise ValueError(
                f"{signal}: vector {index} uses disallowed driver/sampler "
                f"{driver}->{sampler}"
            )

        drive = require_bit(vector.get("drive"), "drive", signal)
        expect = require_bit(vector.get("expect"), "expect", signal)
        if drive != expect:
            raise ValueError(f"{signal}: vector {index} drive/expect mismatch")

        normalized = (driver, sampler, drive, expect)
        if normalized in seen_vectors[signal]:
            raise ValueError(f"{signal}: duplicate vector {normalized}")
        seen_vectors[signal].add(normalized)

    for signal, row in jumpers_by_signal.items():
        required = {
            (driver, sampler, value, value)
            for driver, sampler in allowed_driver_sampler_pairs(row)
            for value in (0, 1)
        }
        if seen_vectors[signal] != required:
            missing = required - seen_vectors[signal]
            extra = seen_vectors[signal] - required
            details = []
            if missing:
                details.append(f"missing {sorted(missing)}")
            if extra:
                details.append(f"extra {sorted(extra)}")
            raise ValueError(f"{signal}: incomplete first-pass test vectors: {'; '.join(details)}")


def validate_generated_scripts(manifest):
    expected_outputs = generate_connectivity_scripts.expected_outputs(manifest)
    stale = generate_connectivity_scripts.check_outputs(expected_outputs)
    if stale:
        raise ValueError("generated connectivity scripts are stale: " + "; ".join(stale))

    for controller, path in generate_connectivity_scripts.OUTPUTS.items():
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            try:
                command = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_number}: invalid JSON: {exc}") from exc
            if command.get("controller") != controller:
                raise ValueError(
                    f"{path}:{line_number}: command controller "
                    f"{command.get('controller')!r} does not match script {controller!r}"
                )


def validate_dry_run(manifest):
    commands = dry_run_connectivity.load_commands()
    dry_run_connectivity.dry_run(manifest, commands)


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

    jumpers_by_signal = {}
    for row in jumpers:
        signal = row.get("signal")
        if not isinstance(signal, str) or not SIGNAL_RE.fullmatch(signal):
            raise ValueError(f"bad stable signal name: {signal!r}")
        if signal in jumpers_by_signal:
            raise ValueError(f"duplicate signal name: {signal}")
        jumpers_by_signal[signal] = row

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

    validate_first_pass_test_plan(manifest, jumpers_by_signal)
    validate_generated_scripts(manifest)
    validate_dry_run(manifest)

    print(
        f"validated {len(jumpers)} gpio connectivity manifest rows "
        f"and {len(manifest['first_pass_test_plan'])} first-pass test vectors; "
        "generated scripts are current; dry-run passed"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
