[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+(\.\d+)?$')]
    [string] $Version = '1.0.0',
    [string] $CompilerPath,
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$release = Join-Path $root 'build\DrumBreaker_artefacts\Release'
$bundle = Join-Path $release 'VST3\DrumBreaker.vst3'
$binary = Join-Path $bundle 'Contents\x86_64-win\DrumBreaker.vst3'
$standalone = Join-Path $release 'Standalone\DrumBreaker.exe'
$runtimeDirectory = Join-Path $root 'build\installer-dependencies'
$runtime = Join-Path $runtimeDirectory 'vc_redist.x64.exe'
$webViewRuntime = Join-Path $runtimeDirectory 'MicrosoftEdgeWebview2Setup.exe'

if ([string]::IsNullOrWhiteSpace($CompilerPath)) {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) {
        $CompilerPath = $command.Source
    } else {
        $candidates = @(
            "${env:ProgramFiles}\Inno Setup 7\ISCC.exe",
            "${env:ProgramFiles(x86)}\Inno Setup 7\ISCC.exe",
            "${env:ProgramFiles}\Inno Setup 6\ISCC.exe",
            "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
        )
        $CompilerPath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    }
}
if (-not $CompilerPath -or -not (Test-Path -LiteralPath $CompilerPath -PathType Leaf)) {
    throw 'Install Inno Setup 6.3 or later, or specify -CompilerPath with the path to ISCC.exe.'
}

function Assert-X64Binary([string] $Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    $reader = [System.IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Not a Windows PE binary: $Path" }
        $stream.Position = 0x3C
        $header = $reader.ReadUInt32()
        $stream.Position = $header
        if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x8664) {
            throw "Expected an x64 Windows release binary: $Path"
        }
    } finally {
        $reader.Dispose()
    }
}

if (-not $SkipBuild) {
    $cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if (-not $cmake) { throw 'CMake is not available on PATH.' }
    & $cmake.Source --build (Join-Path $root 'build') --config Release --target DrumBreaker_Standalone DrumBreaker_VST3
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed. Close the DAW/standalone app and check the build errors.' }
}

foreach ($path in @($binary, $standalone, (Join-Path $bundle 'Contents\Resources\moduleinfo.json'))) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing release artifact: $path" }
}
Assert-X64Binary $binary
Assert-X64Binary $standalone
# JUCE's moduleinfo writer emits trailing commas, which Windows PowerShell rejects.
$metadataJson = Get-Content -LiteralPath (Join-Path $bundle 'Contents\Resources\moduleinfo.json') -Raw
$metadata = ($metadataJson -replace ',(\s*[}\]])', '$1') | ConvertFrom-Json
if ($metadata.Name -cne 'DrumBreaker' -or $metadata.'Factory Info'.Vendor -cne 'Hidden Audio' -or
    @($metadata.Classes | Where-Object { $_.Name -cne 'DrumBreaker' -or $_.Vendor -cne 'Hidden Audio' }).Count -gt 0) {
    throw 'VST3 metadata must identify DrumBreaker by Hidden Audio. Rebuild the plugin before packaging.'
}
$versionInfo = (Get-Item -LiteralPath $standalone).VersionInfo
if ($versionInfo.ProductName -cne 'DrumBreaker' -or $versionInfo.CompanyName -cne 'Hidden Audio') {
    throw 'Standalone metadata must identify DrumBreaker by Hidden Audio. Rebuild the app before packaging.'
}

New-Item -ItemType Directory -Path $runtimeDirectory -Force | Out-Null
if (-not (Test-Path -LiteralPath $runtime)) {
    $download = "$runtime.download"
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri 'https://aka.ms/vc14/vc_redist.x64.exe' -OutFile $download -UseBasicParsing
        Move-Item -LiteralPath $download -Destination $runtime
    } finally {
        if (Test-Path -LiteralPath $download) { Remove-Item -LiteralPath $download }
    }
}
if (-not (Test-Path -LiteralPath $webViewRuntime)) {
    $download = "$webViewRuntime.download"
    try {
        Invoke-WebRequest -Uri 'https://go.microsoft.com/fwlink/p/?LinkId=2124703' -OutFile $download -UseBasicParsing
        Move-Item -LiteralPath $download -Destination $webViewRuntime
    } finally {
        if (Test-Path -LiteralPath $download) { Remove-Item -LiteralPath $download }
    }
}
foreach ($prerequisite in @($runtime, $webViewRuntime)) {
    $signature = Get-AuthenticodeSignature -LiteralPath $prerequisite
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)') {
        throw "Prerequisite is not signed by Microsoft Corporation: $prerequisite. Remove the cached download and retry."
    }
}
$installerSource = Join-Path $PSScriptRoot 'DrumBreaker.iss'
& $CompilerPath "/DAppVersion=$Version" "/DRuntimeInstaller=$runtime" "/DWebViewRuntimeInstaller=$webViewRuntime" $installerSource
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }
$output = Join-Path $root "dist\DrumBreaker-Setup-$Version-Windows-x64.exe"
if (-not (Test-Path -LiteralPath $output)) { throw 'Compiler did not produce the expected installer.' }
Get-FileHash -LiteralPath $output -Algorithm SHA256 | Format-List
Write-Host "Installer ready: $output"
Write-Host 'Compilation only: no installer or prerequisite was executed.'