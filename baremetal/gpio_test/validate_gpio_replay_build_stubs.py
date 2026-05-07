#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# validate_gpio_replay_build_stubs.py --- Host checks for GPIO replay stubs.
# Copyright (c) 2026 Jakob Kastelic
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


BASE = Path(__file__).resolve().parent
REPO = BASE.parents[2]
MANIFEST = BASE / "connectivity_manifest.json"
STUBS = {
    "mpu": {
        "source": BASE / "gpio_replay_mpu_stub.c",
        "header": "connectivity_mpu_replay.h",
    },
    "fpga": {
        "source": REPO / "fpga" / "gpio_replay_fpga_stub.c",
        "header": "connectivity_fpga_replay.h",
    },
}
EXPECTED_QSPI_STARTUP_DRIVE_MAPPINGS = {
    "mpu_qspi_ncs_to_fpga_cs_n": ("QSPI_NCS_PORT", "QSPI_NCS_PIN"),
}
EXPECTED_QSPI_REPORT_DRIVE_MAPPINGS = {
    "mpu_qspi_clk_to_fpga_sclk": (
        "mpu_sclk_drive_signal",
        "QSPI_CLK_PORT",
        "QSPI_CLK_PIN",
    ),
    "mpu_qspi_io0_to_fpga_io0": (
        "mpu_io0_drive_signal",
        "GPIOH",
        "GPIO_PIN_3",
    ),
    "mpu_qspi_io1_to_fpga_io1": (
        "mpu_io1_drive_signal",
        "GPIOF",
        "GPIO_PIN_9",
    ),
    "mpu_qspi_io2_to_fpga_io2": (
        "mpu_io2_drive_signal",
        "GPIOH",
        "GPIO_PIN_6",
    ),
    "mpu_qspi_io3_to_fpga_io3": (
        "mpu_io3_drive_signal",
        "GPIOH",
        "GPIO_PIN_7",
    ),
}
EXPECTED_QSPI_SAMPLE_MAPPINGS = {
    "mpu_qspi_io0_to_fpga_io0": ("GPIOH", "GPIO_PIN_3"),
    "mpu_qspi_io1_to_fpga_io1": ("GPIOF", "GPIO_PIN_9"),
    "mpu_qspi_io2_to_fpga_io2": ("GPIOH", "GPIO_PIN_6"),
    "mpu_qspi_io3_to_fpga_io3": ("GPIOH", "GPIO_PIN_7"),
}
EXPECTED_QSPI_MPU_SIGNALS = (
    set(EXPECTED_QSPI_STARTUP_DRIVE_MAPPINGS)
    | set(EXPECTED_QSPI_REPORT_DRIVE_MAPPINGS)
)


class BuildStubError(ValueError):
    pass


def require_include(source, header):
    text = source.read_text(encoding="utf-8")
    include_re = re.compile(rf'^\s*#\s*include\s+"{re.escape(header)}"\s*$', re.M)
    if not include_re.search(text):
        raise BuildStubError(f"{source}: missing direct include of {header}")


def role_allows_drive(role):
    return role in {"drive", "drive_sample"}


def role_allows_sample(role):
    return role in {"sample", "drive_sample"}


def load_manifest_qspi_jumpers():
    try:
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise BuildStubError(f"{MANIFEST}: cannot load manifest: {exc}") from exc

    jumpers = manifest.get("jumpers")
    if not isinstance(jumpers, list):
        raise BuildStubError(f"{MANIFEST}: manifest must contain a jumpers list")

    qspi = {}
    for row in jumpers:
        if not isinstance(row, dict):
            raise BuildStubError(f"{MANIFEST}: jumper rows must be objects")
        signal = row.get("signal")
        if isinstance(signal, str) and signal.startswith("mpu_qspi_"):
            if signal in qspi:
                raise BuildStubError(f"{MANIFEST}: duplicate QSPI jumper {signal}")
            qspi[signal] = row

    expected = EXPECTED_QSPI_MPU_SIGNALS
    if set(qspi) != expected:
        missing = sorted(expected - set(qspi))
        extra = sorted(set(qspi) - expected)
        details = []
        if missing:
            details.append(f"missing {missing}")
        if extra:
            details.append(f"extra {extra}")
        raise BuildStubError(f"{MANIFEST}: unexpected QSPI jumpers: {'; '.join(details)}")

    return qspi


def parse_mpu_signal_table(source_text, table_name):
    table_re = re.compile(
        rf"static\s+const\s+mpu_gpio_signal_t\s+{re.escape(table_name)}\[\]\s*="
        r"\s*\{(?P<body>.*?)\};",
        re.DOTALL,
    )
    table_match = table_re.search(source_text)
    if table_match is None:
        raise BuildStubError(f"gpio_replay_mpu_stub.c: missing {table_name}[]")

    row_re = re.compile(
        r'\{\s*"(?P<signal>[^"]+)"\s*,\s*'
        r"(?P<port>[A-Z][A-Z0-9_]*)\s*,\s*"
        r"(?P<pin>GPIO_PIN_[0-9]+|[A-Z][A-Z0-9_]*_PIN)\s*\}"
    )
    entries = {}
    for row_match in row_re.finditer(table_match.group("body")):
        signal = row_match.group("signal")
        if signal in entries:
            raise BuildStubError(f"{table_name}[]: duplicate signal {signal}")
        entries[signal] = (row_match.group("port"), row_match.group("pin"))

    if not entries:
        raise BuildStubError(f"{table_name}[]: no signal mappings found")

    return entries


def parse_mpu_signal_object(source_text, object_name):
    object_re = re.compile(
        rf"static\s+const\s+mpu_gpio_signal_t\s+{re.escape(object_name)}\s*="
        r"\s*\{\s*"
        r'"(?P<signal>[^"]+)"\s*,\s*'
        r"(?P<port>[A-Z][A-Z0-9_]*)\s*,\s*"
        r"(?P<pin>GPIO_PIN_[0-9]+|[A-Z][A-Z0-9_]*_PIN)\s*"
        r"\};",
        re.DOTALL,
    )
    object_match = object_re.search(source_text)
    if object_match is None:
        raise BuildStubError(f"gpio_replay_mpu_stub.c: missing {object_name}")
    return (
        object_match.group("signal"),
        object_match.group("port"),
        object_match.group("pin"),
    )


def require_qspi_mpu_manifest_coverage():
    qspi = load_manifest_qspi_jumpers()
    stub = STUBS["mpu"]["source"].read_text(encoding="utf-8")
    drive_entries = parse_mpu_signal_table(stub, "mpu_drive_signals")
    sample_entries = parse_mpu_signal_table(stub, "mpu_sample_signals")
    report_drive_entries = {}
    for signal, (object_name, port, pin) in EXPECTED_QSPI_REPORT_DRIVE_MAPPINGS.items():
        actual_signal, actual_port, actual_pin = parse_mpu_signal_object(stub, object_name)
        if (actual_signal, actual_port, actual_pin) != (signal, port, pin):
            raise BuildStubError(
                f"{object_name}: expected {signal}, {port}, {pin}; got "
                f"{actual_signal}, {actual_port}, {actual_pin}"
            )
        report_drive_entries[signal] = (actual_port, actual_pin)

    dedicated_drive_signals = set(EXPECTED_QSPI_REPORT_DRIVE_MAPPINGS)
    generic_drive_conflicts = sorted(dedicated_drive_signals & set(drive_entries))
    if generic_drive_conflicts:
        raise BuildStubError(
            "mpu_drive_signals[] must not contain report-only QSPI mappings: "
            f"{generic_drive_conflicts}"
        )

    for signal, row in qspi.items():
        mpu_role = row.get("mpu_role")
        if signal in EXPECTED_QSPI_STARTUP_DRIVE_MAPPINGS:
            expected_drive = EXPECTED_QSPI_STARTUP_DRIVE_MAPPINGS[signal]
            actual_drive = drive_entries.get(signal)
        else:
            _, expected_port, expected_pin = EXPECTED_QSPI_REPORT_DRIVE_MAPPINGS[signal]
            expected_drive = (expected_port, expected_pin)
            actual_drive = report_drive_entries.get(signal)
        if role_allows_drive(mpu_role) and actual_drive != expected_drive:
            raise BuildStubError(
                f"{signal}: missing MPU drive mapping "
                f"{expected_drive[0]}, {expected_drive[1]}"
            )
        expected_sample = EXPECTED_QSPI_SAMPLE_MAPPINGS.get(signal)
        if role_allows_sample(mpu_role) and (
            expected_sample is None or sample_entries.get(signal) != expected_sample
        ):
            raise BuildStubError(
                f"{signal}: missing MPU sample mapping"
            )
        if not role_allows_drive(mpu_role) and not role_allows_sample(mpu_role):
            raise BuildStubError(f"{signal}: invalid MPU role {mpu_role!r}")


def compile_and_run(name, source, out_dir):
    cc = os.environ.get("CC", "cc")
    exe = out_dir / f"{name}_replay_stub"
    cmd = [
        cc,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-DGPIO_REPLAY_STUB_MAIN",
        "-I",
        str(BASE),
        str(source),
        "-o",
        str(exe),
    ]
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe)], check=True)


def main():
    try:
        with tempfile.TemporaryDirectory(prefix="gpio-replay-stubs-") as tmp:
            out_dir = Path(tmp)
            for name, stub in STUBS.items():
                source = stub["source"]
                require_include(source, stub["header"])
                compile_and_run(name, source, out_dir)
            require_qspi_mpu_manifest_coverage()
    except (BuildStubError, OSError, subprocess.CalledProcessError) as exc:
        print(f"gpio replay build stub validation failed: {exc}", file=sys.stderr)
        return 1

    print(
        "gpio replay build stubs consume generated replay headers; "
        "MPU QSPI manifest coverage passed"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
