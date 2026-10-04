# Snapshot a QML surface offscreen: no window is shown, no mouse is moved, no desktop is grabbed.
#
# The surfaces have no main window and only appear on a trigger, so the established way to look
# at one is to drive the real app with the pointer and photograph the screen (ui-*.ps1). That
# moves the mouse and captures whatever else happens to be on the desktop. This tool takes the
# other road: the QTest cases already build the real QML in a Qt Quick scene, so running them on
# Qt's "offscreen" QPA platform renders that scene into a QImage with no window ever shown and
# no input touched.
#
# The mechanism the cases share is two pieces, both offscreen-agnostic:
#   - setup.cpp loads the Windows UI fonts by file with QFontDatabase::addApplicationFont and sets
#     the application font. The offscreen platform ships no fonts, so without this every glyph
#     draws as a box; it also hands each engine a `lensQaSnapshotDir` context property taken from
#     LENS_QA_SNAPSHOT_DIR below.
#   - testutil.js `saveSnapshot(testCase, item, dir, name)` calls the case's own grabImage(item)
#     and writes <dir>/<name>.png; the case guards on an empty dir and skips.
# This script sets that environment, runs the target, and reports where the PNGs landed.
#
# It verifies what a surface *looks like* -- glyphs, weight, alignment, colour -- not where it is
# placed or how it reacts. Layout and interaction stay with lens_qtest_surfaces (TEST.md section
# 2) and the real-surface tools above. TEST.md section 5 owns the write-up.
#
# Examples:
#   ./scripts/qml-snapshot.ps1
#   ./scripts/qml-snapshot.ps1 -Test test_captureChineseAndLatinTypography
#   ./scripts/qml-snapshot.ps1 -Target lens_qtest_surfaces -Out test/records/snapshots/surfaces

[CmdletBinding()]
param(
    [string]$BuildDir = "build-ninja",
    [ValidateSet("lens_qtest_components", "lens_qtest_surfaces")]
    [string]$Target = "lens_qtest_components",
    # A test-function selector, forwarded to the runner as-is. The cases that write snapshots
    # all skip when LENS_QA_SNAPSHOT_DIR is unset, so leaving this out just runs everything and
    # only the snapshot cases leave files behind.
    [string]$Test,
    [string]$Out = "test/records/snapshots",
    [string]$QtBin
)

$ErrorActionPreference = "Stop"

function Get-RepositoryRoot {
    $root = (& git rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($root)) {
        throw "qml-snapshot: run this script inside the repository"
    }
    return $root.Trim()
}

# The Qt the tree was configured against, read from the qmllint response file CMake writes: its
# second include path ends in \qml, whose parent is the Qt root that carries bin\. This keeps the
# DLLs on PATH matching the tree instead of a hard-coded install.
function Get-QtBin([string]$root, [string]$build, [string]$requested) {
    if (-not [string]::IsNullOrWhiteSpace($requested)) {
        return $requested
    }

    $responseDir = Join-Path $build "src\app\.rcc\qmllint"
    $response = Get-ChildItem -LiteralPath $responseDir -Filter "*.rsp" -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $response) {
        $qmlDir = Get-Content -LiteralPath $response.FullName -Encoding UTF8 |
            Where-Object { $_ -match '[/\\]qml$' } |
            Select-Object -First 1
        if (-not [string]::IsNullOrWhiteSpace($qmlDir)) {
            $bin = Join-Path (Split-Path -Parent $qmlDir.Trim()) "bin"
            if (Test-Path -LiteralPath $bin) {
                return $bin
            }
        }
    }

    return (Get-LensPaths -Root $root).qtBin
}

# Machine paths come from config/paths.json; config/README.md owns the rule.
. "$PSScriptRoot\paths.ps1"

$root = Get-RepositoryRoot

$build = $BuildDir
if (-not [System.IO.Path]::IsPathRooted($build)) {
    $build = Join-Path $root $build
}
$executable = Join-Path $build "test\qtest\$Target.exe"
if (-not (Test-Path -LiteralPath $executable)) {
    throw "qml-snapshot: build the target first, $executable not found"
}

$qtBin = Get-QtBin $root $build $QtBin
if (-not (Test-Path -LiteralPath (Join-Path $qtBin "Qt6Quick.dll"))) {
    throw "qml-snapshot: no Qt6Quick.dll under $qtBin; pass -QtBin or build the tree first"
}

$outDir = $Out
if (-not [System.IO.Path]::IsPathRooted($outDir)) {
    $outDir = Join-Path $root $outDir
}
if (-not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

# The env is read by setup.cpp (LENS_QA_SNAPSHOT_DIR) and by Qt itself (the platform plugin).
# It lives only in this process and is inherited by the runner below.
$env:PATH = "$qtBin;$env:PATH"
$env:QT_QPA_PLATFORM = "offscreen"
$env:QT_FORCE_STDERR_LOGGING = "1"
$env:LENS_QA_SNAPSHOT_DIR = $outDir

$arguments = @()
if (-not [string]::IsNullOrWhiteSpace($Test)) {
    $arguments += $Test
}

Write-Output "qml-snapshot: running $Target offscreen -> $outDir"
& $executable @arguments
$runnerExit = $LASTEXITCODE

$saved = @(Get-ChildItem -LiteralPath $outDir -Filter *.png -File -ErrorAction SilentlyContinue)
foreach ($file in $saved) {
    Write-Output "qml-snapshot: $($file.FullName)"
}
Write-Output "qml-snapshot: $($saved.Count) PNG(s) in $outDir (runner exit $runnerExit)"

exit $runnerExit
