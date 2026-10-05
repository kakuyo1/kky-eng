# Sample injected mouse input while the app starts, and report every stall.
#
# The low-level mouse hook runs on the thread that installed it, and Windows holds the input for
# that thread until it answers -- past LowLevelHooksTimeout (300 ms by default) it gives up and
# the click is lost. A stall here is therefore not "the app is slow", it is a mouse the reader
# cannot steer. mouse_event is the same path the shell takes, and sampling with no sleep keeps
# the resolution fine enough to tell a hiccup from a freeze.
#
# This is the regression check for the hook's own thread (docs/QML.md section 5). It found the
# bug -- three consecutive calls at 311.9 / 311.6 / 311.7 ms, right on the timeout -- and later
# confirmed the fix: 0 calls over 60 ms, worst 27.7 ms. Re-run it whenever the hook or the
# startup path changes.
#
# The pointer is nudged one pixel back and forth for the whole run, so run it where a stray move
# does not matter. Nothing is clicked.
#
# Examples:
#   ./scripts/qa/mouse-stall-probe.ps1
#   ./scripts/qa/mouse-stall-probe.ps1 -App build-ninja-perf/src/app/lens.exe -Seconds 30
param(
    [string]$App = 'build-ninja/src/app/lens.exe',  # relative to the repository root, or absolute
    [string]$QtBin,                 # the app needs the Qt DLLs on PATH; default is config/paths.json
    [int]$Seconds = 20,             # how long to sample for
    [double]$LaunchAfter = 1.5,     # seconds of baseline before the app is started
    [double]$ThresholdMs = 60       # report a call slower than this
)

Add-Type -Namespace U -Name W -MemberDefinition @'
[DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
'@

$MOVE = 0x0001

$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$exe = if ([IO.Path]::IsPathRooted($App)) { $App } else { Join-Path $root $App }
if (-not (Test-Path $exe)) { throw "no executable at $exe" }

# The Qt DLLs the app links against: -QtBin overrides, otherwise config/paths.json, which is the
# one place a machine path is written (config/README.md).
. "$PSScriptRoot\..\build\paths.ps1"
if ([string]::IsNullOrWhiteSpace($QtBin)) { $QtBin = (Get-LensPaths -Root $root).qtBin }
if (Test-Path $QtBin) { $env:PATH = "$QtBin;$env:PATH" }

$t0 = [Diagnostics.Stopwatch]::StartNew()
$started = $false
$worst = 0.0
$stalls = 0
$d = 1

while ($t0.Elapsed.TotalSeconds -lt $Seconds) {
    if (-not $started -and $t0.Elapsed.TotalSeconds -ge $LaunchAfter) {
        # The launch line carries our clock, so the app's own log timestamps can be lined up
        # with it afterwards.
        Write-Output ("[{0,7:N0} ms] launching {1} at {2:HH:mm:ss.fff}" -f $t0.Elapsed.TotalMilliseconds, $exe, (Get-Date))
        Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe)
        $started = $true
    }

    $sw = [Diagnostics.Stopwatch]::StartNew()
    [U.W]::mouse_event($MOVE, $d, 0, 0, [IntPtr]::Zero)
    $sw.Stop()
    $d = -$d

    $ms = $sw.Elapsed.TotalMilliseconds
    if ($ms -gt $worst) { $worst = $ms }
    if ($ms -gt $ThresholdMs) {
        $stalls++
        Write-Output ("[{0,7:N0} ms] input took {1,7:N1} ms  (ended {2:HH:mm:ss.fff})" -f $t0.Elapsed.TotalMilliseconds, $ms, (Get-Date))
    }
    Start-Sleep -Milliseconds 5
}

Write-Output ("worst single call: {0:N1} ms over {1} stalls" -f $worst, $stalls)
