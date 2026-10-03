#!/bin/sh
# Lint the QML surfaces, and hold the warning count to a ratchet.
#
# A loose .qml file has no module around it, so qmllint cannot resolve Tokens or the components
# in qml/components/, and reports every read off each of them: 525 warnings on this tree,
# almost all of them that one mistake. The module only resolves when qmllint is handed the
# module directory and the .qrc files that turn the module's `prefer :/qt/qml/Lens/` back into
# files on disk. CMake writes exactly that argument list to the response file below when it
# configures the tree, so this reads that file instead of assembling the list itself and
# drifting from it.
#
# Needs a configured build tree, and nothing else: qmllint is taken from the Qt the tree was
# configured against. When either is missing it says so and passes, the same contract the
# other checks in .githooks/pre-commit keep.
#
# The count is a ratchet, like the typography budgets in that hook: lower it when you fix
# warnings, never raise it to get a commit through.
#
# Usage: scripts/qml-lint.sh [--report]
#   --report  print every warning even when the count is under the baseline

set -u

BASELINE=130

root=$(git rev-parse --show-toplevel 2>/dev/null)
[ -n "$root" ] || root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 0

build=""
for candidate in build-ninja build; do
    if [ -f "$candidate/src/app/.rcc/qmllint/lens.rsp" ]; then
        build="$candidate"
        break
    fi
done
if [ -z "$build" ]; then
    echo "qml-lint: no configured build tree with a qmllint response file; configure one first (skip)"
    exit 0
fi
rsp="$build/src/app/.rcc/qmllint/lens.rsp"

# The response file names the Qt it was configured against as its second include path, which is
# also where that Qt's qmllint lives. Using that one guarantees the lint matches the tree.
qtQml=$(awk '/^-I$/ { getline; if ($0 ~ /[\/\\]qml$/) { print; exit } }' "$rsp")
qmllint="${qtQml%/qml}/bin/qmllint.exe"
if [ ! -x "$qmllint" ]; then
    qmllint=$(command -v qmllint 2>/dev/null || true)
fi
if [ -z "$qmllint" ]; then
    echo "qml-lint: qmllint is neither beside the configured Qt nor on PATH (skip)"
    exit 0
fi

report=$("$qmllint" "@$rsp" 2>&1)
count=$(printf '%s\n' "$report" | grep -cE '^Warning: ')

if [ "${1:-}" = "--report" ]; then
    printf '%s\n' "$report"
fi

if [ "$count" -le "$BASELINE" ]; then
    echo "qml-lint: $count warnings, baseline $BASELINE"
    exit 0
fi

printf '%s\n' "$report"
echo
echo "qml-lint: $count warnings, baseline $BASELINE -- the baseline only goes down;"
echo "  fix warnings rather than raise it. A delegate usually wants a required property,"
echo "  and ids reached from a nested component want 'pragma ComponentBehavior: Bound'."
exit 1
