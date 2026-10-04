# Run qmllint under a working-set ceiling and a wall-clock ceiling.
#
# qmllint 6.9.0 grows without bound on some input -- a regex literal inside a loop is one, and
# MixedText.qml carries the note. Unbounded it takes the whole machine down: it passed 1.5 GB
# four seconds in, measured. A clock alone is not a guard here, because the growth is faster
# than any timeout worth waiting for on a normal machine; so this watches the working set.
#
# Prints the lint output on stdout, exactly as qmllint does. Exit 0 when the run finished,
# 3 when it was killed, 4 when qmllint could not be started at all -- the caller has to tell a
# kill from a verdict, and a check that treats a kill as a pass is the failure this file exists
# to prevent.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Rsp,
    [int]$CapMB = 1024,
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$out = [IO.Path]::GetTempFileName()
$err = [IO.Path]::GetTempFileName()

try {
    $cap = $CapMB * 1MB
    $sw = [Diagnostics.Stopwatch]::StartNew()
    try {
        $p = Start-Process -FilePath $Exe -ArgumentList "@$Rsp" -PassThru -NoNewWindow `
            -RedirectStandardOutput $out -RedirectStandardError $err
    } catch {
        [Console]::Error.WriteLine("qml-lint: could not start $Exe -- $_")
        exit 4
    }

    $killed = $null
    while (-not $p.HasExited) {
        $p.Refresh()
        if ($p.WorkingSet64 -gt $cap) { $killed = "working set passed $CapMB MB"; break }
        if ($sw.Elapsed.TotalSeconds -gt $TimeoutSeconds) {
            $killed = "still running after $TimeoutSeconds s at $([math]::Round($p.WorkingSet64 / 1MB)) MB"
            break
        }
        Start-Sleep -Milliseconds 100
    }
    if ($killed) { try { $p.Kill() } catch { } }

    Get-Content $out -ErrorAction SilentlyContinue
    Get-Content $err -ErrorAction SilentlyContinue

    if ($killed) {
        [Console]::Error.WriteLine("qml-lint: qmllint was killed, $killed. That is not a verdict -- it is")
        [Console]::Error.WriteLine("  a failure. qmllint 6.9.0 does this on some input; a regex literal inside a")
        [Console]::Error.WriteLine("  loop is the one found so far. Raise the cap only to look, never to pass.")
        exit 3
    }
    exit 0
} finally {
    Remove-Item $out, $err -ErrorAction SilentlyContinue
}
