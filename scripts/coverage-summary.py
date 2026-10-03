#!/usr/bin/env python3
"""Roll a merged Cobertura report up to one line per directory.

The report OpenCppCoverage writes has one <package> per module it instrumented, and a source
file that several test binaries all link appears once in each of them. The root element's
line-rate sums those copies, so it counts a shared file two or three times over and reads low.
This walks the tree once and unions the line numbers per file instead, which is also what makes
two runs comparable.

Usage: python scripts/coverage-summary.py test/records/coverage/coverage.xml
"""

from __future__ import annotations

import sys
import xml.etree.ElementTree as ET
from collections import defaultdict

# The repository's own sources, below the drive letter the report names them by. The leading
# separator keeps a directory named something like "src-old" from matching first.
MARK = "\\src\\"


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: coverage-summary.py <coverage.xml>")

    covered: dict[str, set[int]] = defaultdict(set)
    valid: dict[str, set[int]] = defaultdict(set)

    for cls in ET.parse(sys.argv[1]).iter("class"):
        filename = cls.get("filename", "")
        if MARK not in filename:
            continue
        relative = filename.split(MARK, 1)[1].replace("\\", "/")
        for line in cls.iter("line"):
            number = int(line.get("number"))
            valid[relative].add(number)
            if int(line.get("hits")) > 0:
                covered[relative].add(number)

    by_directory: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    for relative, lines in valid.items():
        directory = "src/" + relative.split("/")[0]
        by_directory[directory][0] += len(covered[relative])
        by_directory[directory][1] += len(lines)

    def rate(hit: int, total: int) -> str:
        return f"{hit / total * 100:5.1f}%" if total else "    -"

    for directory in sorted(by_directory):
        hit, total = by_directory[directory]
        print(f"coverage: {directory:14s} {hit:5d}/{total:<5d} {rate(hit, total)}")

    hit = sum(value[0] for value in by_directory.values())
    total = sum(value[1] for value in by_directory.values())
    print(f"coverage: {'total':14s} {hit:5d}/{total:<5d} {rate(hit, total)}  ({len(valid)} files)")


if __name__ == "__main__":
    main()
