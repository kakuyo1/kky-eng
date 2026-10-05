[CmdletBinding()]
param(
    [string]$BuildDir = "build-ninja-qml-profile",
    [Parameter(Mandatory = $true)]
    [string]$Output,
    [string[]]$ApplicationArguments = @(),
    [switch]$Interactive,
    [ValidateSet("full", "startup", "tray", "navigation", "settings", "bubble")]
    [string]$Scenario = "full",
    [string]$Include = "javascript,memory,pixmapcache,scenegraph,animations,painting,compiling,creating,binding,handlingsignal,inputevents,debugmessages"
)

$ErrorActionPreference = "Stop"

function Get-RepositoryRoot {
    $root = (& git rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($root)) {
        throw "qml-profile: run this script inside the repository"
    }
    return $root.Trim()
}

# Machine paths come from config/paths.json; config/README.md owns the rule.
. "$PSScriptRoot\..\build\paths.ps1"

$root = Get-RepositoryRoot
$build = $BuildDir
if (-not [System.IO.Path]::IsPathRooted($build)) {
    $build = Join-Path $root $build
}
$executable = Join-Path $build "src\app\lens.exe"
if (-not (Test-Path -LiteralPath $executable)) {
    throw "qml-profile: executable not found: $executable"
}

$profiler = (Get-Command qmlprofiler.exe -ErrorAction SilentlyContinue).Source
if ([string]::IsNullOrWhiteSpace($profiler)) {
        $responseDir = Join-Path $build "src\app\.rcc\qmllint"
        $response = Get-ChildItem -LiteralPath $responseDir -Filter "*.rsp" -File -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($null -ne $response) {
            $qmlDir = Get-Content -LiteralPath $response.FullName -Encoding UTF8 |
                Where-Object { $_ -match '[/\\]qml$' } |
                Select-Object -First 1
        if (-not [string]::IsNullOrWhiteSpace($qmlDir)) {
            $qtRoot = Split-Path -Parent $qmlDir.Trim()
            $profiler = Join-Path $qtRoot "bin\qmlprofiler.exe"
        }
    }
}
if (-not (Test-Path -LiteralPath $profiler)) {
    $profiler = Join-Path (Get-LensPaths -Root $root).qtBin "qmlprofiler.exe"
}
if (-not (Test-Path -LiteralPath $profiler)) {
    throw "qml-profile: qmlprofiler.exe is neither on PATH nor at the configured Qt path"
}

$outputPath = $Output
if (-not [System.IO.Path]::IsPathRooted($outputPath)) {
    $outputPath = Join-Path $root $outputPath
}
$parent = Split-Path -Parent $outputPath
if (-not (Test-Path -LiteralPath $parent)) {
    throw "qml-profile: output directory does not exist: $parent"
}

$profilerArguments = @("--output", $outputPath, "--include", $Include)
if ($Interactive) {
    $profilerArguments += "--interactive"
}
$previousScenario = $env:LENS_QML_PROFILE_SCENARIO
if ($Scenario -ne "full") {
    $env:LENS_QML_PROFILE_SCENARIO = $Scenario
}
$profilerArguments += $executable
$profilerArguments += $ApplicationArguments

Write-Output "qml-profile: starting $executable"
Write-Output "qml-profile: writing $outputPath"
& $profiler @profilerArguments
$env:LENS_QML_PROFILE_SCENARIO = $previousScenario
exit $LASTEXITCODE
