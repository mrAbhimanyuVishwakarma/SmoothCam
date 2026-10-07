<#
.SYNOPSIS
    Authenticode-signs SmoothCam binaries with SHA-256 and an RFC 3161 timestamp.

.DESCRIPTION
    Uses a code-signing certificate from the current user's store (or a hardware
    token exposed through it). The timestamp keeps signatures valid after the
    certificate expires, which matters for a release that won't be rebuilt.

    Configure once per machine:
        $env:SMOOTHCAM_CERT_THUMBPRINT = '<thumbprint of your code-signing cert>'
        $env:SMOOTHCAM_TIMESTAMP_URL   = 'http://timestamp.digicert.com'   # or your CA's TSA

.EXAMPLE
    .\scripts\sign.ps1 -Files dist\SmoothCam_beta.exe
#>
param(
    [Parameter(Mandatory)] [string[]]$Files,
    [string]$Thumbprint = $env:SMOOTHCAM_CERT_THUMBPRINT,
    [string]$TimestampUrl = $(if ($env:SMOOTHCAM_TIMESTAMP_URL) { $env:SMOOTHCAM_TIMESTAMP_URL } else { 'http://timestamp.digicert.com' })
)

$ErrorActionPreference = 'Stop'
if (-not $Thumbprint) { throw 'Set SMOOTHCAM_CERT_THUMBPRINT (or pass -Thumbprint) to sign.' }

$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $signtool) { throw 'signtool.exe not found (install the Windows SDK).' }

foreach ($file in $Files) {
    & $signtool.FullName sign /sha1 $Thumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 /d 'SmoothCam' $file
    if ($LASTEXITCODE) { throw "signing $file failed" }
    & $signtool.FullName verify /pa /q $file
    if ($LASTEXITCODE) { throw "verifying $file failed" }
}
