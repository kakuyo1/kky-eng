[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$Output
)

$ErrorActionPreference = "Stop"

function Get-RepositoryRoot {
    $root = (& git rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($root)) {
        throw "qml-metrics: run this script inside the repository"
    }
    return $root.Trim()
}

function Get-ResponseFile([string]$root, [string]$requestedBuildDir) {
    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($requestedBuildDir)) {
        $candidates += $requestedBuildDir
    } else {
        $candidates += "build-ninja"
        $candidates += "build"
    }

    foreach ($candidate in $candidates) {
        $build = $candidate
        if (-not [System.IO.Path]::IsPathRooted($build)) {
            $build = Join-Path $root $build
        }
        $responseDir = Join-Path $build "src\app\.rcc\qmllint"
        $response = Get-ChildItem -LiteralPath $responseDir -Filter "*.rsp" -File -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($null -ne $response) {
            return [pscustomobject]@{
                BuildDir = (Resolve-Path -LiteralPath $build).Path
                Response = $response.FullName
            }
        }
    }

    throw "qml-metrics: no configured build tree with a QML qmllint response file"
}

function Get-QmlMetrics([string]$path, [string]$root) {
    $lines = @(Get-Content -LiteralPath $path -Encoding UTF8)
    $blank = @($lines | Where-Object { [string]::IsNullOrWhiteSpace($_) }).Count

    $codeLines = New-Object System.Collections.Generic.List[string]
    $comments = 0
    $inBlockComment = $false
    foreach ($line in $lines) {
        $trimmed = $line.Trim()
        if ($inBlockComment) {
            $comments++
            $codeLines.Add("")
            if ($trimmed -match '\*/') {
                $inBlockComment = $false
            }
            continue
        }
        if ($trimmed -match '^//') {
            $comments++
            $codeLines.Add("")
            continue
        }
        if ($trimmed -match '^/\*') {
            $comments++
            $codeLines.Add("")
            if ($trimmed -notmatch '\*/') {
                $inBlockComment = $true
            }
            continue
        }
        $codeLines.Add($line)
    }

    $codeText = $codeLines -join "`n"
    $objects = @($codeLines | Where-Object { $_ -match '^\s*[A-Z][A-Za-z0-9_.]*\s*\{' }).Count
    $methods = [regex]::Matches($codeText, '\bfunction\s+[A-Za-z_]\w*\s*\(').Count
    $handlers = [regex]::Matches($codeText, '\bon[A-Z][A-Za-z0-9_]*\s*:').Count
    $properties = [regex]::Matches($codeText, '^\s*property\b', [System.Text.RegularExpressions.RegexOptions]::Multiline).Count
    $signals = [regex]::Matches($codeText, '^\s*signal\b', [System.Text.RegularExpressions.RegexOptions]::Multiline).Count
    $conditions = [regex]::Matches($codeText, '\b(if|for|while)\s*\(').Count
    $logicalOperators = [regex]::Matches($codeText, '&&|\|\|').Count
    $loaders = [regex]::Matches($codeText, '^\s*Loader\s*\{', [System.Text.RegularExpressions.RegexOptions]::Multiline).Count
    $listViews = [regex]::Matches($codeText, '^\s*ListView\s*\{', [System.Text.RegularExpressions.RegexOptions]::Multiline).Count
    $repeaters = [regex]::Matches($codeText, '^\s*Repeater\s*\{', [System.Text.RegularExpressions.RegexOptions]::Multiline).Count
    $connections = [regex]::Matches($codeText, '^\s*Connections\s*\{', [System.Text.RegularExpressions.RegexOptions]::Multiline).Count
    $timers = [regex]::Matches($codeText, '^\s*Timer\s*\{', [System.Text.RegularExpressions.RegexOptions]::Multiline).Count
    $behaviors = [regex]::Matches($codeText, '^\s*Behavior\s+on\b', [System.Text.RegularExpressions.RegexOptions]::Multiline).Count
    $animations = [regex]::Matches($codeText, '^\s*(NumberAnimation|PropertyAnimation|SmoothedAnimation|SpringAnimation|.*Animator)\s*\{', [System.Text.RegularExpressions.RegexOptions]::Multiline).Count
    $effects = [regex]::Matches($codeText, '^\s*(MultiEffect|ShaderEffect|layer\.enabled)\b', [System.Text.RegularExpressions.RegexOptions]::Multiline).Count
    $anchors = [regex]::Matches($codeText, '^\s*anchors\.', [System.Text.RegularExpressions.RegexOptions]::Multiline).Count
    $bindings = @($codeLines | Where-Object {
        $_ -match '^\s*[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*\s*:' -and
        $_ -notmatch '^\s*(property|signal|on[A-Z])\b'
    }).Count

    $depth = 0
    $maxDepth = 0
    foreach ($line in $codeLines) {
        $depth += ([regex]::Matches($line, '\{')).Count
        if ($depth -gt $maxDepth) {
            $maxDepth = $depth
        }
        $depth -= ([regex]::Matches($line, '\}')).Count
    }

    $rootPrefix = $root.TrimEnd([char[]]@("\", "/")) + [System.IO.Path]::DirectorySeparatorChar
    $relative = $path.Substring($rootPrefix.Length).Replace("\", "/")
    return [pscustomobject]@{
        File = $relative
        PhysicalLoc = $lines.Count
        BlankLoc = $blank
        CommentLoc = $comments
        CodeLoc = $lines.Count - $blank - $comments
        QmlObjects = $objects
        Methods = $methods
        SignalHandlers = $handlers
        PropertyDeclarations = $properties
        SignalDeclarations = $signals
        PropertyBindings = $bindings
        Conditions = $conditions
        LogicalOperators = $logicalOperators
        LexicalBraceDepth = $maxDepth
        Loaders = $loaders
        ListViews = $listViews
        Repeaters = $repeaters
        Connections = $connections
        Timers = $timers
        Behaviors = $behaviors
        Animations = $animations
        Effects = $effects
        AnchorBindings = $anchors
    }
}

function Get-QmlLintJson([string]$qmllint, [string]$response) {
    $jsonText = (& $qmllint "@$response" --json - 2>$null) -join "`n"
    $exitCode = $LASTEXITCODE
    if ([string]::IsNullOrWhiteSpace($jsonText)) {
        throw "qml-metrics: qmllint produced no JSON output (exit code $exitCode)"
    }

    return [pscustomobject]@{
        ExitCode = $exitCode
        Data = ($jsonText | ConvertFrom-Json)
    }
}

$root = Get-RepositoryRoot
$buildInfo = Get-ResponseFile $root $BuildDir
$responseLines = @(Get-Content -LiteralPath $buildInfo.Response -Encoding UTF8)
$qmlPaths = @($responseLines | Where-Object { $_ -match '\.qml$' } | ForEach-Object { $_.Trim() })
if ($qmlPaths.Count -eq 0) {
    throw "qml-metrics: response file contains no QML files"
}

$qmlFiles = @($qmlPaths | ForEach-Object {
    $path = $_
    if (-not [System.IO.Path]::IsPathRooted($path)) {
        $path = Join-Path $root $path
    }
    if (-not (Test-Path -LiteralPath $path)) {
        throw "qml-metrics: QML file from response file does not exist: $path"
    }
    Get-QmlMetrics (Resolve-Path -LiteralPath $path).Path $root
})

$qmlDir = $responseLines | Where-Object { $_ -match '[/\\]qml$' } | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($qmlDir)) {
    throw "qml-metrics: could not locate the Qt QML directory in the response file"
}
$qtRoot = Split-Path -Parent $qmlDir.Trim()
$qmllint = Join-Path $qtRoot "bin\qmllint.exe"
if (-not (Test-Path -LiteralPath $qmllint)) {
    $qmllint = (Get-Command qmllint.exe -ErrorAction SilentlyContinue).Source
}
if ([string]::IsNullOrWhiteSpace($qmllint) -or -not (Test-Path -LiteralPath $qmllint)) {
    throw "qml-metrics: qmllint is neither beside the configured Qt nor on PATH"
}

$lint = Get-QmlLintJson $qmllint $buildInfo.Response
$lintFiles = @($lint.Data.files)
$diagnostics = @($lintFiles | Where-Object { $null -ne $_.warnings } | ForEach-Object { @($_.warnings) })
$diagnosticGroups = @($diagnostics | Group-Object id | Sort-Object Name)
$byId = [ordered]@{}
foreach ($group in $diagnosticGroups) {
    $byId[$group.Name] = $group.Count
}

$byFile = @($lintFiles | ForEach-Object {
    $warningCount = if ($null -eq $_.warnings) { 0 } else { @($_.warnings).Count }
    [pscustomobject]@{
        File = $_.filename
        Success = [bool]$_.success
        DiagnosticCount = $warningCount
    }
} | Sort-Object -Property @{Expression = "DiagnosticCount"; Descending = $true}, File)

$summary = [pscustomobject]@{
    FileCount = $qmlFiles.Count
    PhysicalLoc = ($qmlFiles | Measure-Object -Property PhysicalLoc -Sum).Sum
    CodeLoc = ($qmlFiles | Measure-Object -Property CodeLoc -Sum).Sum
    BlankLoc = ($qmlFiles | Measure-Object -Property BlankLoc -Sum).Sum
    CommentLoc = ($qmlFiles | Measure-Object -Property CommentLoc -Sum).Sum
    QmlObjects = ($qmlFiles | Measure-Object -Property QmlObjects -Sum).Sum
    Methods = ($qmlFiles | Measure-Object -Property Methods -Sum).Sum
    SignalHandlers = ($qmlFiles | Measure-Object -Property SignalHandlers -Sum).Sum
    PropertyDeclarations = ($qmlFiles | Measure-Object -Property PropertyDeclarations -Sum).Sum
    SignalDeclarations = ($qmlFiles | Measure-Object -Property SignalDeclarations -Sum).Sum
    PropertyBindings = ($qmlFiles | Measure-Object -Property PropertyBindings -Sum).Sum
    Conditions = ($qmlFiles | Measure-Object -Property Conditions -Sum).Sum
    LogicalOperators = ($qmlFiles | Measure-Object -Property LogicalOperators -Sum).Sum
    LexicalBraceDepth = ($qmlFiles | Measure-Object -Property LexicalBraceDepth -Maximum).Maximum
    Loaders = ($qmlFiles | Measure-Object -Property Loaders -Sum).Sum
    ListViews = ($qmlFiles | Measure-Object -Property ListViews -Sum).Sum
    Repeaters = ($qmlFiles | Measure-Object -Property Repeaters -Sum).Sum
    Connections = ($qmlFiles | Measure-Object -Property Connections -Sum).Sum
    Timers = ($qmlFiles | Measure-Object -Property Timers -Sum).Sum
    Behaviors = ($qmlFiles | Measure-Object -Property Behaviors -Sum).Sum
    Animations = ($qmlFiles | Measure-Object -Property Animations -Sum).Sum
    Effects = ($qmlFiles | Measure-Object -Property Effects -Sum).Sum
    AnchorBindings = ($qmlFiles | Measure-Object -Property AnchorBindings -Sum).Sum
    QmllintDiagnostics = $diagnostics.Count
    QmllintFailedFiles = @($lintFiles | Where-Object { -not $_.success }).Count
    QmllintDiagnosticsById = $byId
}

$result = [pscustomobject]@{
    GeneratedAt = (Get-Date).ToString("o")
    Tool = [pscustomobject]@{
        Name = "qml-metrics.ps1"
        Qmllint = ((& $qmllint --version 2>&1) -join " ").Trim()
    }
    Scope = [pscustomobject]@{
        BuildDir = $buildInfo.BuildDir
        ResponseFile = $buildInfo.Response
        Files = $qmlFiles
    }
    Summary = $summary
    Qmllint = [pscustomobject]@{
        ExitCode = $lint.ExitCode
        Files = $byFile
    }
}

$json = $result | ConvertTo-Json -Depth 8
if ([string]::IsNullOrWhiteSpace($Output)) {
    $json
} else {
    $outputPath = $Output
    if (-not [System.IO.Path]::IsPathRooted($outputPath)) {
        $outputPath = Join-Path $root $outputPath
    }
    $parent = Split-Path -Parent $outputPath
    if (-not (Test-Path -LiteralPath $parent)) {
        throw "qml-metrics: output directory does not exist: $parent"
    }
    Set-Content -LiteralPath $outputPath -Value $json -Encoding UTF8
    Write-Output "qml-metrics: wrote $outputPath"
}
