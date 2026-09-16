# Packages an already built FlashGram into GitHub release assets:
#   release\<version>\FlashGram-Setup-<version>.exe
#   release\<version>\FlashGram-Portable-<version>.zip
#   release\<version>\SHA256SUMS.txt, README_RELEASE.txt, RELEASE_NOTES.md
# It never builds, never touches dependencies and never prints credentials.

param(
    [string]$Version = "1.0.3-fixed",
    # Numeric x.y.z.w for Windows version resources (suffixes are not allowed).
    [string]$FileVersion = "1.0.3.1",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$Iscc = "D:\Inno Setup 6\ISCC.exe",
    [switch]$SkipPortable
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$BuildExe = Join-Path $Root "build\$Configuration\Telegram.exe"
$BuildModules = Join-Path $Root "build\$Configuration\modules\x64\d3d\d3dcompiler_47.dll"
$Staging = Join-Path $Root "release-staging"
$Output = Join-Path $Root "release\$Version"

function Fail($message) {
    Write-Host "[FlashGram] ERROR: $message" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $BuildExe)) { Fail "$BuildExe not found, build first." }
if (-not (Test-Path $BuildModules)) { Fail "$BuildModules not found." }
if (-not (Test-Path $Iscc)) { Fail "Inno Setup compiler not found at $Iscc." }

$running = Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.Path -and $_.Path.StartsWith($Root, [StringComparison]::OrdinalIgnoreCase)
}
if ($running) { Fail "Close FlashGram started from $Root before packaging." }

$info = (Get-Item $BuildExe).VersionInfo
if ($info.ProductVersion -ne $Version) {
    Fail "EXE ProductVersion is '$($info.ProductVersion)', expected '$Version'."
}

Write-Host "[FlashGram] Staging runtime files..."
if (Test-Path $Staging) { Remove-Item -LiteralPath $Staging -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $Staging "modules\x64\d3d") | Out-Null
Copy-Item $BuildExe (Join-Path $Staging "FlashGram.exe")
Copy-Item $BuildModules (Join-Path $Staging "modules\x64\d3d\d3dcompiler_47.dll")
Copy-Item (Join-Path $Root "LICENSE") (Join-Path $Staging "LICENSE.txt")

$allowed = @("FlashGram.exe", "d3dcompiler_47.dll", "LICENSE.txt")
$unexpected = Get-ChildItem $Staging -Recurse -File | Where-Object { $allowed -notcontains $_.Name }
if ($unexpected) { Fail "Unexpected files in staging: $($unexpected.FullName -join ', ')" }

Write-Host "[FlashGram] Scanning FlashGram.exe for secrets..."
$bytes = [IO.File]::ReadAllBytes((Join-Path $Staging "FlashGram.exe"))
$ascii = [Text.Encoding]::ASCII.GetString($bytes)
$utf16 = [Text.Encoding]::Unicode.GetString($bytes)
$bytes = $null
# OpenSSL and Qt contain PEM header constants ("-----BEGIN PRIVATE KEY-----")
# as plain strings, so only a header followed by base64 key material counts.
$pemBlock = '-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----\r?\n[A-Za-z0-9+/=]{40,}'
if ([regex]::IsMatch($ascii, $pemBlock)) { Fail "PEM private key block found in FlashGram.exe" }
$patterns = @("service_role", "sb_secret_", "SUPABASE_SERVICE_ROLE", "BOT_TOKEN=")
$apiJson = Join-Path $Root "build\Debug\flashgram_api.json"
if (Test-Path $apiJson) {
    $hash = (Get-Content $apiJson -Raw | ConvertFrom-Json).api_hash
    if ($hash) { $patterns += $hash.ToLowerInvariant(); $patterns += $hash.ToUpperInvariant() }
}
$index = 0
foreach ($pattern in $patterns) {
    $index++
    if ($ascii.Contains($pattern) -or $utf16.Contains($pattern)) {
        $label = if ($index -gt 4) { "api_hash (plain)" } else { $pattern }
        Fail "Forbidden value found in FlashGram.exe: $label"
    }
}
$ascii = $null; $utf16 = $null; $hash = $null
[GC]::Collect()
Write-Host "[FlashGram] Secret scan passed."

New-Item -ItemType Directory -Force $Output | Out-Null
Get-ChildItem $Output -File | Remove-Item -Force

Write-Host "[FlashGram] Building installer..."
& $Iscc /Q "/DMyAppVersion=$Version" "/DMyAppFileVersion=$FileVersion" "/DStagingPath=$Staging" "/DOutputPath=$Output" `
    (Join-Path $Root "Telegram\build\flashgram_setup.iss")
if ($LASTEXITCODE -ne 0) { Fail "ISCC failed with exit code $LASTEXITCODE." }

if (-not $SkipPortable) {
    Write-Host "[FlashGram] Creating portable zip..."
    Compress-Archive -Path (Join-Path $Staging "*") `
        -DestinationPath (Join-Path $Output "FlashGram-Portable-$Version.zip") `
        -CompressionLevel Optimal
}

# Per-version notes live in release_notes\<version>.md (UTF-8) and are copied
# byte for byte, so non-ASCII text survives any PowerShell edition.
$notesSource = Join-Path $Root "release_notes\$Version.md"
$notes = @"
FlashGram $Version Fix

- New FlashGram icon
- New welcome screen
- The Open Emojis integration
- FlashGram server connection
- Stability fixes
- UI fixes
- Security improvements
- Simplified Windows installation
"@
if (Test-Path $notesSource) {
    Copy-Item $notesSource (Join-Path $Output "RELEASE_NOTES.md") -Force
} else {
    Set-Content -Path (Join-Path $Output "RELEASE_NOTES.md") -Value $notes -Encoding utf8
}

$readme = @"
FlashGram $Version for Windows 10/11 x64

Install:
  1. Download FlashGram-Setup-$Version.exe.
  2. Run it, choose the install folder and finish the setup.
  3. Start FlashGram from the Start menu or the desktop shortcut.
  4. Log in to Telegram as usual.

Nothing else is needed: no Visual Studio, Qt, Python or extra DLLs.

Portable (optional): unpack FlashGram-Portable-$Version.zip and run FlashGram.exe.

FlashGram is an unofficial client based on Telegram Desktop (GPLv3).
Telegram login goes directly to Telegram. The FlashGram server only keeps
FlashGram features (FlashGram ID, config and update info) and never receives
your Telegram login codes, passwords, keys or messages.

Verify downloads with SHA256SUMS.txt.
"@
Set-Content -Path (Join-Path $Output "README_RELEASE.txt") -Value $readme -Encoding utf8

$sums = Get-ChildItem $Output -File | Where-Object { $_.Name -ne "SHA256SUMS.txt" } | Sort-Object Name | ForEach-Object {
    "{0} *{1}" -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $_.Name
}
Set-Content -Path (Join-Path $Output "SHA256SUMS.txt") -Value $sums -Encoding ascii

Write-Host "[FlashGram] Release assets:"
Get-ChildItem $Output -File | Format-Table Name, Length -AutoSize
