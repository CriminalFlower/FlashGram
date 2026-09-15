# Writes your Telegram API credentials for FlashGram into
# build\Debug\flashgram_api.json (read at runtime, ignored by git).
# API_HASH is read as a secure string and is never printed.

$ErrorActionPreference = 'Stop'
$targetDir = Join-Path $PSScriptRoot 'build\Debug'
if ($args.Count -gt 0) {
    $targetDir = $args[0]
}
New-Item -ItemType Directory -Force $targetDir | Out-Null
$target = Join-Path $targetDir 'flashgram_api.json'

$apiId = Read-Host 'API_ID (digits)'
if ($apiId -notmatch '^\d+$') {
    Write-Host 'API_ID must contain digits only.'
    exit 1
}

$secure = Read-Host 'API_HASH (input hidden)' -AsSecureString
$bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
try {
    $apiHash = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
} finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
}
if ($apiHash -notmatch '^[0-9a-fA-F]{32}$') {
    Write-Host 'API_HASH must be 32 hex characters.'
    exit 1
}

$content = "{`r`n    `"api_id`": $apiId,`r`n    `"api_hash`": `"$apiHash`"`r`n}`r`n"
[IO.File]::WriteAllText($target, $content, (New-Object Text.UTF8Encoding $false))
$apiHash = $null

Write-Host "Saved credentials to $target (ignored by git)."
