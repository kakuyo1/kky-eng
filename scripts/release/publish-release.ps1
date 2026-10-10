<#
.SYNOPSIS
Build and publish the committed Lens version, with an annotated tag and its installer.
.EXAMPLE
powershell -File scripts/release/publish-release.ps1 -Version 1.1.0 -WhatIf
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [switch]$Draft
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$previousDirectory = Get-Location
$notesPath = $null
$sumsDir = $null
try {
    Set-Location -LiteralPath $root
    $head = & git rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'publish-release: cannot resolve HEAD' }
    $branch = & git symbolic-ref --short HEAD
    if ($LASTEXITCODE -ne 0) { throw 'publish-release: check out a branch before publishing' }
    $status = & git status --porcelain
    if ($LASTEXITCODE -ne 0 -or $status) { throw 'publish-release: commit all changes before publishing' }

    $cmake = Get-Content -LiteralPath 'CMakeLists.txt' -Raw -Encoding UTF8
    if ($cmake -notmatch 'project\(Lens\s+VERSION\s+(\d+\.\d+\.\d+)\s' -or $Matches[1] -ne $Version) {
        throw "publish-release: CMake project version does not match $Version"
    }
    $changelog = Get-Content -LiteralPath 'CHANGELOG.md' -Raw -Encoding UTF8
    $section = [regex]::Match($changelog, "(?ms)^## $([regex]::Escape($Version)) - \d{4}-\d{2}-\d{2}\r?\n(.+?)(?=^## |\z)")
    if (-not $section.Success -or [string]::IsNullOrWhiteSpace($section.Groups[1].Value)) {
        throw "publish-release: CHANGELOG.md has no notes for $Version"
    }

    $repo = & gh repo view --json nameWithOwner --jq .nameWithOwner
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($repo)) {
        throw 'publish-release: cannot resolve GitHub repository; run gh auth login first'
    }
    $tag = "v$Version"
    & git show-ref --verify --quiet "refs/tags/$tag"
    if ($LASTEXITCODE -eq 0) { throw "publish-release: local tag $tag already exists" }
    if ($LASTEXITCODE -ne 1) { throw 'publish-release: cannot check local tags' }
    $remoteTag = & git ls-remote origin "refs/tags/$tag"
    if ($LASTEXITCODE -ne 0) { throw 'publish-release: cannot check remote tags' }
    if ($remoteTag) { throw "publish-release: remote tag $tag already exists" }
    $installer = Join-Path $root "build-ninja-release\installer\Lens-$Version-setup.exe"
    if (-not $PSCmdlet.ShouldProcess("$repo at $head", "Build installer, push $branch and $tag, create GitHub Release")) {
        return
    }

    & "$PSScriptRoot\build-installer.bat"
    if ($LASTEXITCODE -ne 0) { throw 'publish-release: installer build failed' }
    if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
        throw "publish-release: installer was not created: $installer"
    }
    . "$PSScriptRoot\..\build\paths.ps1"
    $qtBin = (Get-LensPaths -Root $root).qtBin
    & (Join-Path $qtBin 'qmllint.exe') '@build-ninja-release/src/app/.rcc/qmllint/lens_app.rsp'
    if ($LASTEXITCODE -ne 0) { throw 'publish-release: release QML lint failed' }
    $builtVersion = Get-Content -LiteralPath 'build-ninja-release/installer/version.iss' -Raw -Encoding UTF8
    if ($builtVersion -notmatch ('#define AppVersion "' + [regex]::Escape($Version) + '"')) {
        throw 'publish-release: generated installer version does not match'
    }
    $status = & git status --porcelain
    if ($LASTEXITCODE -ne 0 -or $status) { throw 'publish-release: build changed tracked or untracked files' }
    $currentHead = & git rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $currentHead -ne $head) { throw 'publish-release: HEAD changed during build' }

    # The update card downloads only an installer that this list names, so every release carries one (PHASE3 4.1).
    # One line, lowercase digest, two spaces: the format the client reads with the checksum parser.
    $sumsDir = Join-Path ([IO.Path]::GetTempPath()) ('lens-sums-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $sumsDir | Out-Null
    $sumsPath = Join-Path $sumsDir 'SHA256SUMS'
    $digest = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText($sumsPath, "$digest  $(Split-Path -Leaf $installer)`n", [Text.UTF8Encoding]::new($false))

    $notesPath = [IO.Path]::GetTempFileName()
    [IO.File]::WriteAllText($notesPath, $section.Groups[1].Value.Trim(), [Text.UTF8Encoding]::new($false))
    & git push origin "HEAD:refs/heads/$branch"
    if ($LASTEXITCODE -ne 0) { throw 'publish-release: branch push failed' }
    & git tag -a $tag -m "Release Lens $Version" $head
    if ($LASTEXITCODE -ne 0) { throw 'publish-release: tag creation failed' }
    & git push origin "refs/tags/$tag"
    if ($LASTEXITCODE -ne 0) { throw 'publish-release: tag push failed; local tag is retained' }
    $arguments = @('release', 'create', $tag, $installer, $sumsPath, '--repo', $repo,
        '--verify-tag', '--title', "Lens $Version", '--notes-file', $notesPath)
    if ($Draft) { $arguments += '--draft' }
    & gh @arguments
    if ($LASTEXITCODE -ne 0) { throw 'publish-release: release creation failed; pushed tag is retained' }
    & gh release view $tag --repo $repo --json url,assets
    if ($LASTEXITCODE -ne 0) { throw 'publish-release: cannot verify release' }
} finally {
    if ($notesPath) { Remove-Item -LiteralPath $notesPath -ErrorAction SilentlyContinue }
    if ($sumsDir) { Remove-Item -LiteralPath $sumsDir -Recurse -ErrorAction SilentlyContinue }
    Set-Location -LiteralPath $previousDirectory
}
