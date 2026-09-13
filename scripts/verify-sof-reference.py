#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Hash-pin one official test input and compare the offline parser's output.

No driver installation, certificate changes, firmware loading or hardware access.
Network is used only with --download; existing fixture files are never replaced.
The upstream binary is not redistributed in this repository or CI artifacts.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inspector", required=True, type=Path)
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--download", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    pin = json.loads((root / "tests/fixtures/sof_glk_reference.json").read_text())
    if not args.fixture.exists():
        if not args.download:
            raise ValueError("Fixture absent; provide it or explicitly request --download.")
        with urllib.request.urlopen(pin["url"], timeout=60) as response:
            data = response.read(pin["size"] + 1)
        if len(data) != pin["size"] or hashlib.sha256(data).hexdigest() != pin["sha256"]:
            raise ValueError("Downloaded reference does not match the reviewed SHA-256 pin.")
        args.fixture.parent.mkdir(parents=True, exist_ok=True)
        with args.fixture.open("xb") as target:
            target.write(data)
    else:
        if args.fixture.stat().st_size != pin["size"]:
            raise ValueError("Existing fixture has the wrong size; it was not overwritten.")
        data = args.fixture.read_bytes()
    actual_hash = hashlib.sha256(data).hexdigest()
    if len(data) != pin["size"] or actual_hash != pin["sha256"]:
        raise ValueError("Fixture hash mismatch; do not use it as the reference image.")
    run = subprocess.run([str(args.inspector.resolve()), str(args.fixture.resolve())],
                         capture_output=True, text=True, timeout=30, check=True)
    report = json.loads(run.stdout)
    if report != pin["expected"]:
        raise ValueError(f"Parser metadata differs from the reviewed fixture: {report!r}")
    report["fixture_sha256"] = actual_hash
    report["reference_commit"] = pin["commit"]
    report["reference_check"] = "PASS"
    rendered = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"SOF_REFERENCE=FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
