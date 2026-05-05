#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path


BASE = Path(__file__).resolve().parent
MANIFEST = BASE / "connectivity_manifest.json"
OUTPUTS = {
    "mpu": BASE / "connectivity_mpu.jsonl",
    "fpga": BASE / "connectivity_fpga.jsonl",
}


def load_manifest(path=MANIFEST):
    return json.loads(path.read_text(encoding="utf-8"))


def command_for_vector(vector, vector_index, op, controller):
    return {
        "op": op,
        "controller": controller,
        "signal": vector["signal"],
        "vector_index": vector_index,
        "drive_value": vector["drive"],
        "expected_value": vector["expect"],
    }


def build_scripts(manifest):
    scripts = {controller: [] for controller in OUTPUTS}
    for vector_index, vector in enumerate(manifest["first_pass_test_plan"]):
        driver = vector["driver"]
        sampler = vector["sampler"]
        scripts[driver].append(command_for_vector(vector, vector_index, "drive", driver))
        scripts[sampler].append(
            command_for_vector(vector, vector_index, "sample_expect", sampler)
        )
    return scripts


def encode_jsonl(commands):
    return "".join(
        json.dumps(command, sort_keys=True, separators=(",", ":")) + "\n"
        for command in commands
    )


def expected_outputs(manifest):
    return {
        controller: encode_jsonl(commands)
        for controller, commands in build_scripts(manifest).items()
    }


def write_outputs(outputs):
    for controller, contents in outputs.items():
        OUTPUTS[controller].write_text(contents, encoding="utf-8")


def check_outputs(outputs):
    stale = []
    for controller, expected in outputs.items():
        path = OUTPUTS[controller]
        try:
            actual = path.read_text(encoding="utf-8")
        except FileNotFoundError:
            stale.append(f"{path}: missing")
            continue
        if actual != expected:
            stale.append(f"{path}: stale")
    return stale


def main():
    parser = argparse.ArgumentParser(
        description="Generate first-pass GPIO connectivity command scripts."
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if generated scripts are missing or stale",
    )
    args = parser.parse_args()

    outputs = expected_outputs(load_manifest())
    if args.check:
        stale = check_outputs(outputs)
        if stale:
            for item in stale:
                print(item, file=sys.stderr)
            return 1
        print("connectivity command scripts are current")
        return 0

    write_outputs(outputs)
    for path in OUTPUTS.values():
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
