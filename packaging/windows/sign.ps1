# Signs Windows binaries with signtool and an RFC 3161 timestamp.
#
# Two supported certificate sources (see docs/RELEASE.md):
#   1. Azure Artifact Signing (Trusted Signing): set NL_SIGN_MODE=azure and
#      NL_AZURE_DLIB (path to Azure.CodeSigning.Dlib.dll) and NL_AZURE_METADATA (metadata.json).
#   2. OV/EV certificate on a hardware token or in the cert store: set NL_SIGN_MODE=cert and
#      NL_CERT_SHA1 (thumbprint of the certificate in the user/machine store).
#
# Usage: ./sign.ps1 -Paths "build\...\Northline Channel.vst3\Contents\x86_64-win\Northline Channel.vst3", "dist\NorthlineChannel-1.0.0-win-x64.exe"

param(
    [Parameter(Mandatory = $true)] [string[]] $Paths,
    [string] $TimestampUrl = "http://timestamp.acs.microsoft.com"
)

$ErrorActionPreference = "Stop"
$signtool = (Get-Command signtool.exe -ErrorAction SilentlyContinue).Source
if (-not $signtool) {
    $kits = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Directory | Sort-Object Name -Descending
    foreach ($k in $kits) {
        $candidate = Join-Path $k.FullName "x64\signtool.exe"
        if (Test-Path $candidate) { $signtool = $candidate; break }
    }
}
if (-not $signtool) { throw "signtool.exe not found (install the Windows SDK)" }

$mode = $env:NL_SIGN_MODE
foreach ($p in $Paths) {
    if (-not (Test-Path $p)) { throw "Not found: $p" }
    if ($mode -eq "azure") {
        & $signtool sign /v /fd SHA256 /tr $TimestampUrl /td SHA256 /dlib $env:NL_AZURE_DLIB /dmdf $env:NL_AZURE_METADATA $p
    } elseif ($mode -eq "cert") {
        & $signtool sign /v /fd SHA256 /tr $TimestampUrl /td SHA256 /sha1 $env:NL_CERT_SHA1 $p
    } else {
        throw "Set NL_SIGN_MODE to 'azure' or 'cert'"
    }
    if ($LASTEXITCODE -ne 0) { throw "signtool failed for $p" }
    & $signtool verify /pa /v $p
    if ($LASTEXITCODE -ne 0) { throw "Signature verification failed for $p" }
}
