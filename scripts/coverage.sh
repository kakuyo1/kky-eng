#!/bin/sh
# Line coverage for the C++ libraries, through OpenCppCoverage.
#
# OpenCppCoverage reads the PDB of a binary that is already built, so there is no coverage
# build tree and no extra build flag: it instruments the Debug objects build-ninja already
# produced. See TEST.md section 4 for the scope and for what this cannot see.
#
# Usage: scripts/coverage.sh [output-dir] [target...]
#   output-dir defaults to test/records/coverage, which is gitignored.
#   Naming one or more targets narrows the run. The default is every target that finishes
#   unattended; CI names the four it can build and leaves lens_gtest_integration out, because
#   that one drives the real mouse and clipboard and a runner has neither.

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build-ninja"

ALL_TARGETS="lens_gtest_unit lens_gtest_perf lens_gtest_integration lens_qtest_components lens_qtest_surfaces"

OUT=${1:-test/records/coverage}
case "$OUT" in
    /* | [A-Za-z]:*) ;;
    *) OUT="$ROOT/$OUT" ;;
esac
if [ "$#" -gt 0 ]; then shift; fi
[ "$#" -gt 0 ] || set -- $ALL_TARGETS

# winget's default location, and the one the installer uses on a runner too. Fall back to a
# PATH entry so a differently-placed install still works.
COV='/c/Program Files/OpenCppCoverage/OpenCppCoverage.exe'
[ -x "$COV" ] || COV=$(command -v OpenCppCoverage 2>/dev/null || true)
[ -n "$COV" ] && [ -x "$COV" ] || {
    echo "coverage: OpenCppCoverage is not installed; TEST.md section 4 has the one-line install" >&2
    exit 1
}
[ -d "$BUILD" ] || { echo "coverage: no build tree at $BUILD; run scripts/build.bat first" >&2; exit 1; }

# Backslashes: OpenCppCoverage rejects a path with forward slashes in it, even on the arguments
# that are not the program name.
win() { cygpath -w "$1"; }

target_path() {
    case "$1" in
        lens_qtest_*) echo "test/qtest/$1.exe" ;;
        *) echo "test/googletest/$1.exe" ;;
    esac
}

# Three of the suites carry EXCLUDE_FROM_ALL, so a tree built the ordinary way has no executable
# for two of the names above. Drop a target that is not there: letting it through fails inside
# OpenCppCoverage on a missing file, and the merge below then names a .cov that was never written.
kept=""
for name in "$@"; do
    if [ -f "$BUILD/$(target_path "$name")" ]; then
        kept="$kept $name"
    else
        echo "coverage: $name is not built, skipping; scripts/build.bat --target $name builds it" >&2
    fi
done
# shellcheck disable=SC2086 # one name per word; the leading space is what makes this split
set -- $kept
[ "$#" -gt 0 ] || { echo "coverage: none of the named targets is built" >&2; exit 1; }

# The development machine has Qt's bin off PATH; a CI runner has it on. Only the first needs
# the prefix, and prefixing a directory that does not exist would shadow nothing but is noise.
if [ -d /b/qtt/6.9.0/msvc2022_64/bin ]; then
    PATH="/b/qtt/6.9.0/msvc2022_64/bin:$PATH"
    export PATH
fi
export QT_FORCE_STDERR_LOGGING=1

mkdir -p "$OUT"
SOURCES=$(win "$ROOT/src")

# Every path handed to the tool below is absolute, and the test targets bake the source
# directory in too, so the working directory is free -- and worth moving, because
# OpenCppCoverage drops a LastCoverageResults.log in whatever one it is started from.
cd "$OUT"

# One export per target, merged at the end. A target that exits non-zero still contributes what
# it did run, so a red case does not throw the run away.
for name in "$@"; do
    echo "coverage: $name"
    QT_QPA_PLATFORM=offscreen "$COV" \
        --sources "$SOURCES" \
        --modules "*$name*" \
        --quiet \
        --export_type "binary:$(win "$OUT/$name.cov")" \
        -- "$(win "$BUILD/$(target_path "$name")")" >"$OUT/$name.log" 2>&1 ||
        echo "coverage: $name exited non-zero; its coverage is counted anyway"
done

merge=""
for name in "$@"; do
    merge="$merge --input_coverage $(win "$OUT/$name.cov")"
done

# shellcheck disable=SC2086 # the argument list is meant to split
"$COV" $merge --sources "$SOURCES" --quiet --export_type "cobertura:$(win "$OUT/coverage.xml")" >/dev/null

python "$ROOT/scripts/coverage-summary.py" "$OUT/coverage.xml"
