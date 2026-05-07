#!/usr/bin/env python3
"""Build and run the SCION/BGP integrated IXP scenarios as one shared test entrypoint."""

from __future__ import annotations

import argparse
import subprocess
import sys


def run_cmd(cmd: list[str]) -> None:
    print(f"\n$ {' '.join(cmd)}")
    result = subprocess.run(cmd, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"Command failed ({result.returncode}): {' '.join(cmd)}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run integrated center-IXP scenarios for SCION and BGP")
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Skip ./waf build and only run scenarios",
    )
    parser.add_argument(
        "--verbose",
        type=int,
        choices=[0, 1],
        default=1,
        help="Pass verbose flag to scenarios (default: 1)",
    )
    args = parser.parse_args()

    try:
                if not args.skip_build:
                        run_cmd(["./waf", "build"])

                run_cmd(["./waf", "--run", f"scion-ixp-integrated-scenario --verbose={args.verbose}"])
                run_cmd(["./waf", "--run", f"bgp-ixp-integrated-scenario --verbose={args.verbose}"])
    except RuntimeError as exc:
                print(str(exc), file=sys.stderr)
                return 1

    print("\nPASS: shared SCION+BGP integrated IXP scenarios completed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
