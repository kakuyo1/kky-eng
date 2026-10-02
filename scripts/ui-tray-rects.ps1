# Where the shell has actually put the taskbar and the notification area, in physical pixels.
#
# This is the ground truth the QML side is compared against when a surface lands in the wrong
# place: it is how the tray icon's reported rectangle was confirmed to be in device-independent
# pixels (its x times 1.25 is the notification area's left edge), and how the auto-hidden taskbar
# was found -- its window rect sits below the bottom of the primary screen, which is why the
# primary screen's own metrics look like there is no taskbar at all.
#
# Examples:
#   ./scripts/ui-tray-rects.ps1
Add-Type -Namespace U -Name W -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr child, string cls, string title);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
public struct RECT { public int L, T, R, B; }
'@
[void][U.W]::SetProcessDpiAwarenessContext([IntPtr]::new(-4))

function Show([string]$what, [IntPtr]$h) {
    if ($h -eq [IntPtr]::Zero) { Write-Output "$what : not found"; return }
    $r = New-Object U.W+RECT
    [void][U.W]::GetWindowRect($h, [ref]$r)
    Write-Output "$what : $($r.L),$($r.T) $($r.R - $r.L)x$($r.B - $r.T)  (right $($r.R), bottom $($r.B))"
}

# One taskbar per monitor: Shell_TrayWnd for the primary, Shell_SecondaryTrayWnd for the rest.
$main = [U.W]::FindWindow("Shell_TrayWnd", $null)
Show "Shell_TrayWnd" $main
Show "TrayNotifyWnd" ([U.W]::FindWindowEx($main, [IntPtr]::Zero, "TrayNotifyWnd", $null))
