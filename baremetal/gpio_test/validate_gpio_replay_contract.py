#!/usr/bin/env python3
import json
import re
import sys
from pathlib import Path


BASE = Path(__file__).resolve().parent
CONTRACT = BASE / "gpio_replay_contract.json"
MANIFEST = BASE / "connectivity_manifest.json"
HEADERS = {
    "mpu": BASE / "connectivity_mpu_replay.h",
    "fpga": BASE / "connectivity_fpga_replay.h",
}


class ContractError(ValueError):
    pass


def load_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ContractError(f"{path}: invalid JSON: {exc}") from exc


def parse_struct_fields(header_text, path):
    match = re.search(
        r"typedef\s+struct\s*\{(?P<body>.*?)\}\s*gpio_connectivity_replay_command_t\s*;",
        header_text,
        flags=re.S,
    )
    if not match:
        raise ContractError(f"{path}: missing gpio_connectivity_replay_command_t typedef")

    fields = []
    for line in match.group("body").splitlines():
        line = line.strip()
        if not line:
            continue
        if not line.endswith(";"):
            raise ContractError(f"{path}: malformed struct field {line!r}")
        declaration = line[:-1].strip()
        field_match = re.match(r"(?P<c_type>.*?)(?P<name>[A-Za-z_][A-Za-z0-9_]*)$", declaration)
        if not field_match:
            raise ContractError(f"{path}: malformed struct field {line!r}")
        fields.append(
            {
                "name": field_match.group("name"),
                "c_type": " ".join(field_match.group("c_type").strip().split()),
            }
        )
    return fields


def parse_replay_commands(header_text, controller, path):
    array_name = f"gpio_connectivity_{controller}_replay"
    match = re.search(
        rf"static\s+const\s+gpio_connectivity_replay_command_t\s+{array_name}\[\]\s*=\s*\{{"
        r"(?P<body>.*?)\n\};",
        header_text,
        flags=re.S,
    )
    if not match:
        raise ContractError(f"{path}: missing {array_name} array")

    commands = []
    row_re = re.compile(
        r'\{\s*"(?P<controller>[^"]+)"\s*,\s*"(?P<signal>[^"]+)"\s*,\s*'
        r"(?P<vector_index>\d+)u\s*,\s*"
        r'"(?P<command_kind>[^"]+)"\s*,\s*'
        r"(?P<drive_value>[01])\s*,\s*(?P<expected_value>[01])\s*\},"
    )
    for line_number, line in enumerate(match.group("body").splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        row = row_re.fullmatch(line)
        if not row:
            raise ContractError(f"{path}: replay row {line_number} has unexpected format")
        command = row.groupdict()
        command["vector_index"] = int(command["vector_index"])
        command["drive_value"] = int(command["drive_value"])
        command["expected_value"] = int(command["expected_value"])
        commands.append(command)
    return commands


def contract_field_list(contract):
    fields = contract.get("replay_header_fields")
    if not isinstance(fields, list) or not fields:
        raise ContractError(f"{CONTRACT}: replay_header_fields must be a non-empty list")
    return [{"name": field.get("name"), "c_type": field.get("c_type")} for field in fields]


def contract_operation_names(contract):
    operations = contract.get("operations")
    if not isinstance(operations, list) or not operations:
        raise ContractError(f"{CONTRACT}: operations must be a non-empty list")
    names = [operation.get("name") for operation in operations]
    if any(not isinstance(name, str) or not name for name in names):
        raise ContractError(f"{CONTRACT}: operation names must be non-empty strings")
    if len(set(names)) != len(names):
        raise ContractError(f"{CONTRACT}: duplicate operation names")
    return set(names)


def validate_contract(contract):
    header_list = contract.get("generated_replay_headers")
    expected_header_list = [path.name for path in HEADERS.values()]
    if header_list != expected_header_list:
        raise ContractError(
            f"{CONTRACT}: generated_replay_headers {header_list!r} != {expected_header_list!r}"
        )

    controllers = contract.get("controllers")
    if not isinstance(controllers, dict):
        raise ContractError(f"{CONTRACT}: controllers must be an object")
    if set(controllers) != set(HEADERS):
        raise ContractError(
            f"{CONTRACT}: controller set {sorted(controllers)} != {sorted(HEADERS)}"
        )

    required_text_keys = ("vector_semantics", "signal_semantics")
    for key in required_text_keys:
        if not isinstance(contract.get(key), str) or not contract[key].strip():
            raise ContractError(f"{CONTRACT}: {key} must document replay semantics")


def validate_headers(contract, manifest):
    expected_fields = contract_field_list(contract)
    expected_ops = contract_operation_names(contract)
    expected_signals = {row["signal"] for row in manifest["jumpers"]}
    expected_vectors = set(range(len(manifest["first_pass_test_plan"])))

    parsed_fields = None
    seen_ops = set()
    seen_controllers = set()
    seen_signals = set()
    seen_vectors = set()
    for controller, path in HEADERS.items():
        text = path.read_text(encoding="utf-8")
        fields = parse_struct_fields(text, path)
        if fields != expected_fields:
            raise ContractError(f"{path}: replay fields {fields!r} != contract {expected_fields!r}")
        if parsed_fields is None:
            parsed_fields = fields
        elif fields != parsed_fields:
            raise ContractError(f"{path}: replay typedef differs from other replay headers")

        for command in parse_replay_commands(text, controller, path):
            if command["controller"] != controller:
                raise ContractError(
                    f"{path}: command controller {command['controller']!r} != {controller!r}"
                )
            seen_ops.add(command["command_kind"])
            seen_controllers.add(command["controller"])
            seen_signals.add(command["signal"])
            seen_vectors.add(command["vector_index"])

    if seen_ops != expected_ops:
        raise ContractError(
            f"generated replay operation names {sorted(seen_ops)} != "
            f"contract {sorted(expected_ops)}"
        )
    if seen_controllers != set(HEADERS):
        raise ContractError(
            f"generated replay controllers {sorted(seen_controllers)} != {sorted(HEADERS)}"
        )
    if seen_signals != expected_signals:
        raise ContractError("generated replay signals differ from connectivity_manifest.json")
    if seen_vectors != expected_vectors:
        raise ContractError("generated replay vector_index set differs from first_pass_test_plan")


def main():
    try:
        contract = load_json(CONTRACT)
        manifest = load_json(MANIFEST)
        validate_contract(contract)
        validate_headers(contract, manifest)
    except ContractError as exc:
        print(f"gpio replay contract validation failed: {exc}", file=sys.stderr)
        return 1

    print("gpio replay contract matches generated replay headers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
