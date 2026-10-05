#!/bin/sh
# Which parts of the QML module the two headless QTest suites reach.
#
# The profiler launches the test binary itself, handing it the QML debug connector on the way,
# so what comes out is coverage by the test suite rather than by a hand-driven session -- which
# is the number worth pairing with the C++ one. Qt has no QML line-coverage tool; TEST.md
# section 4 says why, and what this counts instead.
#
# The same caveat as scripts/quality/coverage.sh applies: it measures an existing build tree, and adds
# nothing to it.
#
# Usage: scripts/quality/qml-coverage.sh [output-dir]

set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD="$ROOT/build-ninja"
OUT=${1:-"$ROOT/test/records/qmlcov"}

# The development machine has Qt's bin off PATH; a CI runner has it on.
if [ -d /b/qtt/6.9.0/msvc2022_64/bin ]; then
    PATH="/b/qtt/6.9.0/msvc2022_64/bin:$PATH"
    export PATH
fi
command -v qmlprofiler >/dev/null 2>&1 || {
    echo "qml-coverage: qmlprofiler is not on PATH; see AGENTS.md for where Qt lives" >&2
    exit 1
}
[ -d "$BUILD" ] || { echo "qml-coverage: no build tree at $BUILD; run scripts/build/build.bat first" >&2; exit 1; }

mkdir -p "$OUT"
export QT_QPA_PLATFORM=offscreen
export QT_FORCE_STDERR_LOGGING=1

# The three event kinds the analysis reads. Asking for less means a smaller trace; asking for
# more (creating, compiling, painting) would only add events that name no binding.
INCLUDE=binding,handlingsignal,javascript

traces=""
for exe in test/qtest/lens_qtest_components.exe test/qtest/lens_qtest_surfaces.exe; do
    name=$(basename "$exe" .exe)
    trace="$OUT/$name.qtd"
    echo "qml-coverage: $name"
    qmlprofiler --output "$(cygpath -m "$trace")" --include "$INCLUDE" "$(cygpath -m "$BUILD/$exe")" \
        >"$OUT/$name.log" 2>&1 ||
        echo "qml-coverage: $name exited non-zero; its trace is read anyway"
    traces="$traces --trace $trace"
done

# shellcheck disable=SC2086 # the list is meant to split
exec node "$ROOT/scripts/quality/qml-coverage.js" $traces --json "$OUT/qml-coverage.json"
