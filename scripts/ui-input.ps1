# Drive the pointer, for the surfaces that only wake up to real input.
#
# The app reads a low-level mouse hook and Windows' own hit testing, not QML's test seams, so
# injected events are the only way to exercise a drag-select, an outside press, or a drag of a
# panel. mouse_event is what the hook sees; SetCursorPos is what the window manager sees.
#
# -Action drag steps the pointer along a straight line, which is what a hand roughly does and
# what keeps the app's own position log readable. -Action dragfast sends relative deltas as fast
# as the script can, which is what caught the runaway drag: at that rate the panel went from
# x=1116 to x=-3688 within a few dozen events, while the stepped drag looked fine.
#
# -Action reveal parks the pointer on the bottom row of the primary screen, which is how an
# auto-hidden taskbar is brought up -- and the only way the shell starts reporting where the tray
# icon is, since it reports nothing while the taskbar is down.
#
# Examples:
#   ./scripts/ui-input.ps1 -Action click -X 1456 -Y 355
#   ./scripts/ui-input.ps1 -Action drag -X 1645 -Y 259 -Dx -180 -Dy -144
#   ./scripts/ui-input.ps1 -Action reveal
param(
    [ValidateSet('move', 'click', 'drag', 'dragfast', 'reveal')] [string]$Action = 'move',
    [int]$X = 0,
    [int]$Y = 0,
    [int]$Dx = 0,       # how far a drag travels from X, Y
    [int]$Dy = 0,
    [int]$Steps = 12,   # the steps it is broken into
    [int]$PauseMs = 12  # and the pause between them
)

Add-Type -Namespace U -Name W -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
[DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
'@
[void][U.W]::SetProcessDpiAwarenessContext([IntPtr]::new(-4))

$LEFT_DOWN = 0x0002
$LEFT_UP = 0x0004
$MOVE = 0x0001

function Press {
    [U.W]::mouse_event($LEFT_DOWN, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 120
}

function Release {
    Start-Sleep -Milliseconds 60
    [U.W]::mouse_event($LEFT_UP, 0, 0, 0, [IntPtr]::Zero)
}

switch ($Action) {
    'move' {
        [void][U.W]::SetCursorPos($X, $Y)
        Write-Output "moved to $X,$Y"
    }
    'click' {
        [void][U.W]::SetCursorPos($X, $Y)
        Start-Sleep -Milliseconds 250
        Press
        Release
        Write-Output "clicked $X,$Y"
    }
    'drag' {
        [void][U.W]::SetCursorPos($X, $Y)
        Start-Sleep -Milliseconds 250
        Press
        for ($i = 1; $i -le $Steps; $i++) {
            [void][U.W]::SetCursorPos($X + [int]($Dx * $i / $Steps), $Y + [int]($Dy * $i / $Steps))
            Start-Sleep -Milliseconds $PauseMs
        }
        Release
        Write-Output "dragged from $X,$Y by $Dx,$Dy in $Steps steps"
    }
    'dragfast' {
        [void][U.W]::SetCursorPos($X, $Y)
        Start-Sleep -Milliseconds 250
        Press
        # Relative deltas with no sleep: Windows coalesces them, which is the dense input a hand
        # produces and the input that broke the first two attempts at this.
        for ($i = 1; $i -le $Steps; $i++) {
            [U.W]::mouse_event($MOVE, [int]($Dx / $Steps), [int]($Dy / $Steps), 0, [IntPtr]::Zero)
        }
        Release
        Write-Output "dragged fast from $X,$Y by $Dx,$Dy"
    }
    'reveal' {
        # The last row of the primary screen. The shell slides the auto-hidden taskbar up over
        # a few hundred milliseconds, so the wait is part of the action rather than something
        # the caller has to guess: measuring too early reads the taskbar's hidden rectangle,
        # which sits below the bottom of the screen.
        $h = [U.W]::GetSystemMetrics(1)
        $w = [U.W]::GetSystemMetrics(0)
        [void][U.W]::SetCursorPos([int]($w / 2), $h - 1)
        Start-Sleep -Milliseconds 900
        Write-Output "pointer parked on the bottom edge; the taskbar should be up"
    }
}
