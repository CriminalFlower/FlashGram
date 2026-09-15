# Writes local Telegram API credentials for FlashGram builds.
# The resulting flashgram.credentials.cmake is ignored by git.
# API_HASH is read as a secure string and is never printed.

$ErrorActionPreference = 'Stop'
$target = Join-Path $PSScriptRoot 'flashgram.credentials.cmake'

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

$content = "set(TDESKTOP_API_ID $apiId)`r`nset(TDESKTOP_API_HASH `"$apiHash`")`r`n"
[IO.File]::WriteAllText($target, $content, (New-Object Text.UTF8Encoding $false))
$apiHash = $null

Write-Host "Saved credentials to $target (ignored by git)."
Write-Host 'If the build folder was already configured, delete build\CMakeCache.txt before building.'
