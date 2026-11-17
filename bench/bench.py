#!/usr/bin/env python3
"""Measures what the optimizer buys on each benchmark program.

Runs every bench/*.sprig file with the optimizer off (-O0) and on (-O1) and
prints a Markdown table. Two things are reported:

  * instructions executed: exact and identical on every run, so it is the
    number to trust when comparing -O0 with -O1
  * run time: the fastest of several runs, since the fastest run is the one
    least disturbed by whatever else the machine was doing

Usage:
    python3 bench/bench.py [path/to/sprig] [--runs N]
"""

import argparse
import pathlib
import subprocess
import sys

BENCH_DIR = pathlib.Path(__file__).resolve().parent


def run_once(sprig, program, level):
    """Runs one program and returns (stdout, stats dict parsed from --stats)."""
    result = subprocess.run(
        [str(sprig), level, "--stats", str(program)],
        capture_output=True,
        text=True,
        check=True,
    )
    stats = {}
    for line in result.stderr.splitlines():
        name, _, value = line.partition(": ")
        stats[name] = value
    return result.stdout, stats


def measure(sprig, program, level, runs):
    """Returns (output, instructions executed, bytecode size, best time in ms)."""
    best_ms = float("inf")
    output = instructions = code_bytes = None
    for _ in range(runs):
        output, stats = run_once(sprig, program, level)
        instructions = int(stats["instructions executed"])
        code_bytes = int(stats["bytecode bytes"])
        best_ms = min(best_ms, float(stats["run time ms"]))
    return output, instructions, code_bytes, best_ms


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("sprig", nargs="?", default="build/sprig", help="path to the sprig binary")
    parser.add_argument("--runs", type=int, default=5, help="runs per configuration (default 5)")
    args = parser.parse_args()

    sprig = pathlib.Path(args.sprig)
    if not sprig.exists():
        sys.exit(f"cannot find the sprig binary at '{sprig}'. Build it first (see README).")

    print("| Program | Instructions -O0 | Instructions -O1 | Fewer instructions | Time -O0 | Time -O1 | Speedup |")
    print("|---|---:|---:|---:|---:|---:|---:|")

    for program in sorted(BENCH_DIR.glob("*.sprig")):
        out0, instr0, _, ms0 = measure(sprig, program, "-O0", args.runs)
        out1, instr1, _, ms1 = measure(sprig, program, "-O1", args.runs)

        # An optimization that changes the answer is a bug, not a speedup.
        if out0 != out1:
            sys.exit(f"{program.name}: output differs between -O0 and -O1")

        saved = 100.0 * (instr0 - instr1) / instr0
        print(
            f"| {program.stem} | {instr0:,} | {instr1:,} | {saved:.1f}% "
            f"| {ms0:.0f} ms | {ms1:.0f} ms | {ms0 / ms1:.2f}x |"
        )


if __name__ == "__main__":
    main()
