<#
.SYNOPSIS
    Builds the SmoothCam installer (dist\SmoothCam_beta2.exe) from a release build.

.EXAMPLE
    .\scripts\build.ps1 -Installer          # build + package
    .\scripts\package.ps1                   # package an existing release build
    .\scripts\package.ps1 -Sign             # also sign (see scripts\sign.ps1)
#>
param(
    [string]$BuildDir = 'build\x64-release',
    [string]$OutputName = 'SmoothCam_beta3',
    [string]$VersionLabel = 'Beta 3',
    [switch]$Sign
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $bin = Join-Path $root "$BuildDir\bin"
    foreach ($f in 'SmoothCam.exe', 'SmoothCamSource.dll') {
        if (-not (Test-Path (Join-Path $bin $f))) { throw "$f not found in $bin - build the release preset first." }
    }

    # Version comes from CMake's project() line so there's one source of truth.
    $cmake = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
    if ($cmake -notmatch 'VERSION\s+(\d+\.\d+\.\d+)') { throw 'Could not read the version from CMakeLists.txt' }
    $version = $Matches[1]

    $iscc = @(
        "${env:LOCALAPPDATA}\Programs\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles}\Inno Setup 6\ISCC.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) { throw 'Inno Setup 6 not found. Install it with: winget install JRSoftware.InnoSetup' }

    if ($Sign) {
        & (Join-Path $PSScriptRoot 'sign.ps1') -Files (Join-Path $bin 'SmoothCam.exe'), (Join-Path $bin 'SmoothCamSource.dll')
    }

    $isccArgs = @(
        "/DBuildDir=$bin",
        "/DAppVersion=$version",
        "/DVersionLabel=$VersionLabel",
        "/DOutputName=$OutputName",
        '/Qp',
        'installer\SmoothCam.iss'
    )
    & $iscc @isccArgs
    if ($LASTEXITCODE) { throw "ISCC failed ($LASTEXITCODE)" }

    $setup = Join-Path $root "dist\$OutputName.exe"
    if ($Sign) { & (Join-Path $PSScriptRoot 'sign.ps1') -Files $setup }

    $hash = (Get-FileHash $setup -Algorithm SHA256).Hash
    "$hash  $OutputName.exe" | Set-Content -Encoding ascii (Join-Path $root "dist\$OutputName.exe.sha256")
    $size = [math]::Round((Get-Item $setup).Length / 1MB, 2)
    Write-Host "Installer: $setup ($size MB)"
    Write-Host "SHA-256:   $hash"
}
finally {
    Pop-Location
}
