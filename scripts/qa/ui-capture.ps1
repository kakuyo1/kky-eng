# Capture part of the screen for visual QA, in real pixels.
#
# The process is made per-monitor-v2 aware before it captures anything. An unaware capturer is
# handed a bitmap Windows has already stretched to the monitor's scale, which fakes exactly the
# blur and the offsets these shots exist to check -- it made a 15 px glyph look smeared and a
# panel look half a margin off.
#
# -Virtual takes the whole virtual desktop rather than the primary monitor. That matters here:
# the surfaces do not all live on the primary screen, and GetSystemMetrics(0/1) does not know
# any screen exists.
#
# -Profile prints one row of greys through the middle of the shot, which is what separates a
# crisp stroke from a smeared one: an edge that has been sampled and stretched spreads over more
# pixels and never reaches full darkness.
#
# Examples:
#   ./scripts/qa/ui-capture.ps1 -Out $env:TEMP\a.png -Virtual
#   ./scripts/qa/ui-capture.ps1 -Out $env:TEMP\b.png -Crop 1180,180,560,880
#   ./scripts/qa/ui-capture.ps1 -Out $env:TEMP\c.png -Crop 1800,240,60,60 -Zoom 8 -Profile
param(
    [string]$Out = "$env:TEMP\lens-shot.png",
    # x,y,width,height in screen pixels. A string rather than an int array on purpose: an array
    # parameter arrives as one string when the script is run with `-File`, which is how it is
    # meant to be run, and the failure is a conversion error rather than anything readable.
    [string]$Crop,
    [int]$Zoom = 1,     # nearest neighbour, so a zoomed pixel is a pixel and not an average
    [switch]$Virtual,
    [switch]$Profile
)

Add-Type -Namespace U -Name W -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
[DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
'@
Add-Type -AssemblyName System.Drawing
[void][U.W]::SetProcessDpiAwarenessContext([IntPtr]::new(-4))

if ($Virtual) {
    # 76/77 are the virtual screen's origin, 78/79 its size; 0/1 are the primary monitor alone.
    $x = [U.W]::GetSystemMetrics(76); $y = [U.W]::GetSystemMetrics(77)
    $w = [U.W]::GetSystemMetrics(78); $h = [U.W]::GetSystemMetrics(79)
} else {
    $x = 0; $y = 0
    $w = [U.W]::GetSystemMetrics(0); $h = [U.W]::GetSystemMetrics(1)
}
Write-Output "captured $x,$y ${w}x${h}"

$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
$g.Dispose()

$shot = $bmp
if ($Crop) {
    $parts = @($Crop -split ',' | ForEach-Object { [int]$_.Trim() })
    if ($parts.Count -ne 4) { throw "-Crop takes x,y,width,height" }
    # The crop is given in screen coordinates; pull it back to what was actually captured, or a
    # crop of the virtual desktop would be off by the desktop's origin.
    $rect = New-Object System.Drawing.Rectangle ($parts[0] - $x), ($parts[1] - $y), $parts[2], $parts[3]
    # Checked here because Clone's own complaint about a rectangle that falls outside is "Out of
    # memory", which sends the reader looking in the wrong place entirely.
    if ($rect.X -lt 0 -or $rect.Y -lt 0 -or $rect.Right -gt $w -or $rect.Bottom -gt $h) {
        throw "-Crop $Crop is outside the captured $x,$y ${w}x${h} (x $x..$($x + $w), y $y..$($y + $h))"
    }
    $shot = $bmp.Clone($rect, $bmp.PixelFormat)
}

if ($Zoom -gt 1) {
    $big = New-Object System.Drawing.Bitmap ($shot.Width * $Zoom), ($shot.Height * $Zoom)
    $g2 = [System.Drawing.Graphics]::FromImage($big)
    $g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $g2.DrawImage($shot, 0, 0, $big.Width, $big.Height)
    $g2.Dispose()
    $big.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
    $big.Dispose()
} else {
    $shot.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
}
Write-Output "saved $Out ($($shot.Width)x$($shot.Height) at ${Zoom}x)"

if ($Profile) {
    $mid = [int]($shot.Height / 2)
    $row = ""
    for ($i = 0; $i -lt $shot.Width; $i++) { $row += "{0,3} " -f $shot.GetPixel($i, $mid).R }
    Write-Output "r=$mid R: $row"
}

if ($shot -ne $bmp) { $shot.Dispose() }
$bmp.Dispose()
