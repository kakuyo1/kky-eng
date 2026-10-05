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
# configured against. A machine with no tree at all says so and passes, the same contract the
# other checks in .githooks/pre-commit keep.
#
# A tree that *was* configured and has no response file does not pass. It used to, and that is
# how this step spent its whole life green on CI while checking nothing: the file it looked for
# was left over from before the module moved onto lens_app (docs/adr/0004), CMake does not clean
# up a file it stops generating, so the name went on resolving here and simply did not exist on
# a freshly configured tree. A check that skips and passes is not a check.
#
# The types it judges are the ones that build tree carries. It resolves the module's own .qml
# files through the qmldir among the build's copies of them, not through src/, so this sees a
# .qml type as of the last build: a signal or a property added since then reads as missing on
# whichever file handles it, which is a warning about the copies rather than about the commit.
# CI builds before it lints and never meets that; a commit made straight from the hook does,
# until the tree is next built.
#
# The count is a ratchet, like the typography budgets in that hook: lower it when you fix
# warnings, never raise it to get a commit through.
#
# Usage: scripts/quality/qml-lint.sh [--report]
#   --report  print every warning even when the count is under the baseline

set -u

BASELINE=0

# Working-set ceiling for qmllint, in MB. A healthy run on this tree is 34 MB, so the ceiling is
# not a budget for a heavy lint -- it exists because qmllint 6.9.0 can grow without bound on
# some input (see scripts/quality/qmllint-capped.ps1). Normal runs never come near it.
QML_LINT_CAP_MB=${QML_LINT_CAP_MB:-1024}

root=$(git rev-parse --show-toplevel 2>/dev/null)
[ -n "$root" ] || root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root" || exit 0

# Named for the target the module is backed by, which is lens_app and not the lens executable:
# the response file carries the module's own import paths and its .qrc, so the wrong one lints
# the wrong file list.
rsp_path="src/app/.rcc/qmllint/lens_app.rsp"

build=""
for candidate in build-ninja build; do
    if [ -f "$candidate/$rsp_path" ]; then
        build="$candidate"
        break
    fi
done
if [ -z "$build" ]; then
    for candidate in build-ninja build; do
        if [ -f "$candidate/build.ninja" ]; then
            echo "qml-lint: $candidate is configured but carries no $rsp_path, so the ratchet"
            echo "  cannot run. Reconfigure the tree. If the module's backing target has been"
            echo "  renamed, the name above is what has to change with it."
            exit 1
        fi
    done
    echo "qml-lint: no configured build tree with a qmllint response file; configure one first (skip)"
    exit 0
fi
rsp="$build/$rsp_path"

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
