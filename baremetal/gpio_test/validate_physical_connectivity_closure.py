#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# validate_physical_connectivity_closure.py --- Host closure for GPIO wiring.
# Copyright (c) 2026 Jakob Kastelic
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
MISSION = ROOT / "missions" / "fpga-spi.md"

SETUP_MARKS = {
    "gpio_physical_replay_setup_reset",
    "gpio_physical_replay_setup_precondition",
}
QSPI_CONNECTIVITY_MARKS = {
    "sclk": {
        "gpio_sclk_uart_trigger_blocked_audit",
    },
    "cs_n": {
        "gpio_physical_fpga_ncs_high_audit",
        "gpio_physical_mp135_to_fpga_ncs_low_blocked_audit",
    },
    "io0": {
        "gpio_physical_fpga_io0_drive_audit",
        "gpio_physical_mp135_to_fpga_io0_high_blocked_audit",
        "gpio_physical_mp135_to_fpga_io0_low_blocked_audit",
    },
    "io1": {
        "gpio_physical_fpga_io1_drive_audit",
        "gpio_physical_mp135_to_fpga_io1_high_blocked_audit",
        "gpio_physical_mp135_to_fpga_io1_low_blocked_audit",
    },
    "io2": {
        "gpio_physical_fpga_io2_drive_audit",
        "gpio_physical_mp135_to_fpga_io2_high_blocked_audit",
        "gpio_physical_mp135_to_fpga_io2_low_blocked_audit",
    },
    "io3": {
        "gpio_physical_fpga_io3_drive_audit",
        "gpio_physical_mp135_to_fpga_io3_high_blocked_audit",
        "gpio_physical_mp135_to_fpga_io3_low_blocked_audit",
    },
}
SUPPORTING_AUDIT_MARKS = {
    "gpio_physical_connectivity_audit",
    "gpio_manifest_pin_matches_gpio_nw",
    "gpio_manifest_no_fpga_signal_pin_tbd",
    "gpio_physical_required_tags_reverse_audit",
    "manifest_jumper_pins_are_unique",
    "gpio_replay_mpu_qspi_manifest_coverage",
}
CLOSURE_MARK = "gpio_physical_connectivity_closure"
CLOSURE_BUILD = (
    "python3 stm32mp135_test_board/baremetal/gpio_test/"
    "validate_physical_connectivity_closure.py"
)


class ClosureError(ValueError):
    pass


def above_wip_text(mission):
    marker = "\n## WIP\n"
    count = mission.count(marker)
    if count != 1:
        raise ClosureError(f"{MISSION}: expected exactly one WIP marker, found {count}")
    return mission[: mission.index(marker)]


def find_mark_tags(text):
    return set(re.findall(r"^\s*mark tag=([A-Za-z0-9_]+)\s*$", text, re.M))


def require_marks(found, required, context):
    missing = sorted(required - found)
    if missing:
        raise ClosureError(f"{context}: missing above-WIP mark tags: {missing}")


def require_closure_section(mission):
    match = re.search(
        r"^### Audit physical connectivity closure\n(?P<body>.*?)(?=^### |\Z)",
        mission,
        flags=re.M | re.S,
    )
    if match is None:
        raise ClosureError(f"{MISSION}: missing physical connectivity closure section")
    body = match.group("body")
    if CLOSURE_BUILD not in body:
        raise ClosureError(f"{MISSION}: closure Build block must run {CLOSURE_BUILD}")
    if f"mark tag={CLOSURE_MARK}" not in body:
        raise ClosureError(f"{MISSION}: closure Test block must mark {CLOSURE_MARK}")


def validate_closure(mission_path=MISSION):
    mission = mission_path.read_text(encoding="utf-8", errors="replace")
    above_wip = above_wip_text(mission)
    found_marks = find_mark_tags(above_wip)

    require_marks(found_marks, SETUP_MARKS, "physical setup prerequisites")
    for signal, marks in sorted(QSPI_CONNECTIVITY_MARKS.items()):
        require_marks(found_marks, marks, f"QSPI {signal} connectivity evidence")
    require_marks(found_marks, SUPPORTING_AUDIT_MARKS, "supporting connectivity audits")
    require_closure_section(mission)


def main():
    try:
        validate_closure()
    except (ClosureError, OSError) as exc:
        print(f"physical connectivity closure validation failed: {exc}", file=sys.stderr)
        return 1

    n_connectivity = (
        len(SETUP_MARKS) +
        sum(len(marks) for marks in QSPI_CONNECTIVITY_MARKS.values())
    )
    print(
        "physical connectivity closure validated "
        f"{n_connectivity} connectivity marks and "
        f"{len(SUPPORTING_AUDIT_MARKS)} audits"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
