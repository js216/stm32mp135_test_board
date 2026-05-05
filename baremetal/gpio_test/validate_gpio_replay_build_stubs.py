#!/usr/bin/env python3
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


BASE = Path(__file__).resolve().parent
REPO = BASE.parents[2]
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


class BuildStubError(ValueError):
    pass


def require_include(source, header):
    text = source.read_text(encoding="utf-8")
    include_re = re.compile(rf'^\s*#\s*include\s+"{re.escape(header)}"\s*$', re.M)
    if not include_re.search(text):
        raise BuildStubError(f"{source}: missing direct include of {header}")


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
    except (BuildStubError, subprocess.CalledProcessError) as exc:
        print(f"gpio replay build stub validation failed: {exc}", file=sys.stderr)
        return 1

    print("gpio replay build stubs consume generated replay headers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
