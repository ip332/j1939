#!/usr/bin/env python3
"""Enforce coverage floors and reject any exact-ratio decrease from a PR's base."""
import argparse
from fractions import Fraction
import json
from pathlib import Path
import sys

METRICS = {"line": Fraction("97.8"), "branch": Fraction("93.3"), "function": None}


def read_summary(path):
    data = json.loads(Path(path).read_text())
    result = {}
    for metric in METRICS:
        total, covered = data[metric + "_total"], data[metric + "_covered"]
        if (type(total) is not int or type(covered) is not int
                or total <= 0 or not 0 <= covered <= total):
            raise ValueError(f"{path}: invalid or empty {metric} coverage counts")
        result[metric] = Fraction(covered, total)
    return result


def compare(head, base=None):
    rows = ["| Coverage | Base | PR / main | Change |", "|---|---:|---:|---:|"]
    errors = []
    for metric, minimum in METRICS.items():
        current = head[metric] * 100
        previous = base[metric] * 100 if base is not None else None
        delta = f"{float(current - previous):+.6f} pp" if previous is not None else "—"
        rows.append(f"| {metric.title()} | {float(previous):.6f}% | {float(current):.6f}% | {delta} |"
                    if previous is not None else
                    f"| {metric.title()} | — | {float(current):.6f}% | {delta} |")
        if minimum is not None and current < minimum:
            errors.append(f"{metric} coverage is below the {float(minimum):g}% minimum")
        # Use exact counts rather than gcovr's rounded percentages. Even a small
        # decrease must fail; an unchanged ratio with more code must pass.
        if base is not None and head[metric] < base[metric]:
            errors.append(f"{metric} coverage decreased from the base commit")
    return "\n".join(rows), errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--head", required=True)
    parser.add_argument("--base")
    parser.add_argument("--markdown", help="Write a GitHub Actions job summary")
    args = parser.parse_args()
    try:
        table, errors = compare(read_summary(args.head),
                                read_summary(args.base) if args.base else None)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Coverage comparison failed: {error}", file=sys.stderr)
        return 1
    report = "## Coverage\n\n" + table + "\n"
    if errors:
        report += "\n" + "\n".join(f"- {error}" for error in errors) + "\n"
    print(report)
    if args.markdown:
        with open(args.markdown, "a") as summary:
            summary.write(report)
    return int(bool(errors))


if __name__ == "__main__":
    sys.exit(main())
