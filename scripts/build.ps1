<#
.SYNOPSIS
    Configures, builds and (optionally) tests SmoothCam from a plain PowerShell window.

.EXAMPLE
    .\scripts\build.ps1                     # release build
    .\scripts\build.ps1 -Config debug -Test # debug build + unit tests
    .\scripts\build.ps1 -Installer          # release build + Inno Setup installer
#>
param(
    [ValidateSet('release', 'debug')]
    [string]$Config = 'release',
    [switch]$Test,
    [switch]$Installer,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$preset = "x64-$Config"

function Enter-DevShell {
    if ($env:VSCMD_ARG_TGT_ARCH -eq 'x64') { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'Visual Studio (Build Tools) not found. Install "Desktop development with C++".' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'No Visual Studio install with the x64 C++ tools was found.' }
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

Enter-DevShell
Push-Location $root
try {
    if ($Clean -and (Test-Path "build\$preset")) { Remove-Item -Recurse -Force "build\$preset" }

    cmake --preset $preset
    if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }
    cmake --build --preset $preset
    if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }

    if ($Test) {
        ctest --preset $preset
        if ($LASTEXITCODE) { throw "tests failed ($LASTEXITCODE)" }
    }

    if ($Installer) {
        & (Join-Path $PSScriptRoot 'package.ps1') -BuildDir "build\$preset"
        if ($LASTEXITCODE) { throw "packaging failed ($LASTEXITCODE)" }
    }
}
finally {
    Pop-Location
}
