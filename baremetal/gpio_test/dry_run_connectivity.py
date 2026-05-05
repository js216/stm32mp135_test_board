#!/usr/bin/env python3
import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path


BASE = Path(__file__).resolve().parent
MANIFEST = BASE / "connectivity_manifest.json"
SCRIPTS = {
    "mpu": BASE / "connectivity_mpu.jsonl",
    "fpga": BASE / "connectivity_fpga.jsonl",
}
VALID_OPS = {"drive", "sample_expect"}


class DryRunError(ValueError):
    pass


def load_manifest(path=MANIFEST):
    return json.loads(path.read_text(encoding="utf-8"))


def require_bit(command, field):
    value = command.get(field)
    if isinstance(value, bool) or value not in {0, 1}:
        raise DryRunError(f"{describe(command)}: {field} must be 0 or 1")
    return value


def describe(command):
    source = command.get("_source", "<unknown>")
    line_number = command.get("_line", "?")
    return f"{source}:{line_number}"


def read_jsonl(path, controller):
    commands = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        try:
            command = json.loads(line)
        except json.JSONDecodeError as exc:
            raise DryRunError(f"{path}:{line_number}: invalid JSON: {exc}") from exc
        if not isinstance(command, dict):
            raise DryRunError(f"{path}:{line_number}: command must be an object")
        command["_source"] = str(path)
        command["_line"] = line_number
        command["_script_controller"] = controller
        command["_used"] = False
        commands.append(command)
    return commands


def load_commands(scripts=SCRIPTS):
    commands = []
    for controller, path in scripts.items():
        commands.extend(read_jsonl(path, controller))
    return commands


def validate_command(command, known_signals):
    if command.get("controller") != command["_script_controller"]:
        raise DryRunError(
            f"{describe(command)}: controller {command.get('controller')!r} "
            f"does not match script {command['_script_controller']!r}"
        )

    op = command.get("op")
    if op not in VALID_OPS:
        raise DryRunError(f"{describe(command)}: unknown op {op!r}")

    signal = command.get("signal")
    if signal not in known_signals:
        raise DryRunError(f"{describe(command)}: unknown signal {signal!r}")

    vector_index = command.get("vector_index")
    if isinstance(vector_index, bool) or not isinstance(vector_index, int) or vector_index < 0:
        raise DryRunError(f"{describe(command)}: vector_index must be a non-negative int")

    require_bit(command, "drive_value")
    require_bit(command, "expected_value")
    return vector_index


def command_matches(command, vector, vector_index, op, controller):
    return (
        command.get("vector_index") == vector_index
        and command.get("op") == op
        and command.get("controller") == controller
        and command.get("signal") == vector["signal"]
    )


def take_one(commands, vector, vector_index, op, controller):
    matches = [
        command
        for command in commands
        if not command["_used"] and command_matches(command, vector, vector_index, op, controller)
    ]
    if not matches:
        return None
    if len(matches) > 1:
        locations = ", ".join(describe(command) for command in matches)
        raise DryRunError(
            f"vector {vector_index} {op} {vector['signal']}: duplicate commands at {locations}"
        )
    matches[0]["_used"] = True
    return matches[0]


def dry_run(manifest, commands):
    known_signals = {jumper["signal"] for jumper in manifest["jumpers"]}
    commands_by_vector = defaultdict(list)
    for command in commands:
        vector_index = validate_command(command, known_signals)
        commands_by_vector[vector_index].append(command)

    signal_values = {}
    for vector_index, vector in enumerate(manifest["first_pass_test_plan"]):
        commands_for_vector = commands_by_vector.get(vector_index, [])
        drive = take_one(commands_for_vector, vector, vector_index, "drive", vector["driver"])
        if drive is None:
            raise DryRunError(f"vector {vector_index} {vector['signal']}: missing drive command")
        if drive["drive_value"] != vector["drive"]:
            raise DryRunError(
                f"vector {vector_index} {vector['signal']}: drive value "
                f"{drive['drive_value']} != plan {vector['drive']}"
            )

        signal_values[vector["signal"]] = drive["drive_value"]

        sample = take_one(
            commands_for_vector,
            vector,
            vector_index,
            "sample_expect",
            vector["sampler"],
        )
        if sample is None:
            raise DryRunError(f"vector {vector_index} {vector['signal']}: missing sample command")
        if vector["signal"] not in signal_values:
            raise DryRunError(f"vector {vector_index} {vector['signal']}: sample before drive")
        if sample["expected_value"] != signal_values[vector["signal"]]:
            raise DryRunError(
                f"vector {vector_index} {vector['signal']}: expected "
                f"{sample['expected_value']} != driven {signal_values[vector['signal']]}"
            )

    unused = [command for command in commands if not command["_used"]]
    if unused:
        details = ", ".join(describe(command) for command in unused[:5])
        if len(unused) > 5:
            details += f", ... {len(unused) - 5} more"
        raise DryRunError(f"unused generated command(s): {details}")

    return len(manifest["first_pass_test_plan"])


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Dry-run first-pass GPIO connectivity JSONL command scripts."
    )
    parser.parse_args(argv)

    try:
        vectors = dry_run(load_manifest(), load_commands())
    except DryRunError as exc:
        print(f"connectivity dry-run failed: {exc}", file=sys.stderr)
        return 1

    print(f"connectivity dry-run passed for {vectors} vectors")
    return 0


if __name__ == "__main__":
    sys.exit(main())
