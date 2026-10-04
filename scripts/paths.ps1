# The one reader for config/paths.json.
#
# Every script that needs a machine path comes through here, so the value is written once and a
# machine that differs overrides it in config/paths.local.json instead of editing four files.
# config/README.md owns the rule; docs/adr/0006 has the reasoning and the rejected alternatives.
#
# Deliberately does not resolve the repository root itself. Every caller already knows it, and a
# second Get-RepositoryRoot defined in a dot-sourced file would shadow the one a script defines
# for itself -- qml-profile.ps1 carries its own, and two definitions of one name is how a helper
# silently stops being the helper.

function Get-LensPaths {
    <#
    .SYNOPSIS
        config/paths.json merged with config/paths.local.json, the latter winning key by key.
    .PARAMETER Root
        Repository root holding the config/ directory.
    .OUTPUTS
        A hashtable of name to value; read a key as $paths.qtBin.
    #>
    param(
        [Parameter(Mandatory = $true)][string]$Root
    )

    $merged = @{}
    # Order is the override: 'paths.json' first, so a key in the local file replaces it.
    foreach ($name in @('paths.json', 'paths.local.json')) {
        $file = Join-Path $Root "config/$name"
        if (-not (Test-Path -LiteralPath $file)) { continue }
        $document = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
        foreach ($property in $document.PSObject.Properties) {
            if ($property.Name.StartsWith('_')) { continue }   # the _comment block
            $merged[$property.Name] = $property.Value
        }
    }

    # ${HOME} rather than a user name in a tracked file. Windows only, like the rest of this.
    # USERPROFILE comes back with backslashes; the rest of the file is forward-slashed, and a
    # value that mixes the two reads like corruption in an error message.
    #
    # Not named $home: that is a read-only automatic variable, and assigning to it throws rather
    # than shadowing, which is a strange enough failure to be worth this note.
    $homeDir = $env:USERPROFILE -replace '\\', '/'
    foreach ($key in @($merged.Keys)) {
        if ($merged[$key] -is [string]) {
            $merged[$key] = $merged[$key].Replace('${HOME}', $homeDir)
        }
    }
    return $merged
}

function Get-LensPathList {
    <#
    .SYNOPSIS
        One list-valued key out of the same two files, one item per line.
    .DESCRIPTION
        Written through [Console]::Out rather than returned, because PowerShell's formatter wraps
        an array to the console width and a wrapped path is two paths to whoever reads the lines.
        The commit hook consumes this one line at a time.
    #>
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Key
    )

    $value = (Get-LensPaths -Root $Root).$Key
    if ($null -eq $value) { return }
    if ($value -is [string]) { [Console]::Out.WriteLine($value); return }
    foreach ($item in $value) { [Console]::Out.WriteLine($item) }
}
