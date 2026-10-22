$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($env:DISCORD_SOCIAL_SDK_URL)) {
    throw 'Set the DISCORD_SOCIAL_SDK_URL Actions secret to the official C++ SDK ZIP download URL from the Discord Developer Portal.'
}
if ($env:DISCORD_SOCIAL_SDK_SHA256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'Set the DISCORD_SOCIAL_SDK_SHA256 Actions variable to the SHA-256 hash of that SDK ZIP.'
}
$downloadUri = [uri]$env:DISCORD_SOCIAL_SDK_URL
if (-not $downloadUri.IsAbsoluteUri -or $downloadUri.Scheme -ne 'https') {
    throw 'DISCORD_SOCIAL_SDK_URL must be an HTTPS download URL.'
}

$sdkTemp = Join-Path $env:RUNNER_TEMP "discord-sdk-$([guid]::NewGuid())"
New-Item -ItemType Directory -Path $sdkTemp | Out-Null
$archive = Join-Path $sdkTemp 'sdk.zip'
try {
    Invoke-WebRequest -Uri $downloadUri -OutFile $archive
} catch {
    throw 'Could not download the Discord SDK. Check that DISCORD_SOCIAL_SDK_URL is a working ZIP download URL, not the Developer Portal page.'
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $env:DISCORD_SOCIAL_SDK_SHA256) {
    throw 'Discord SDK checksum mismatch. Verify the ZIP and update DISCORD_SOCIAL_SDK_URL and DISCORD_SOCIAL_SDK_SHA256 together.'
}

$extracted = Join-Path $sdkTemp 'extracted'
Expand-Archive -LiteralPath $archive -DestinationPath $extracted
$sdkRoot = $extracted
if (Test-Path -LiteralPath (Join-Path $extracted 'discord_social_sdk') -PathType Container) {
    $sdkRoot = Join-Path $extracted 'discord_social_sdk'
}
foreach ($file in 'include/discordpp.h', 'include/cdiscord.h', 'lib/release/discord_partner_sdk.lib', 'bin/release/discord_partner_sdk.dll') {
    if (-not (Test-Path -LiteralPath (Join-Path $sdkRoot $file) -PathType Leaf)) {
        throw "The SDK ZIP is missing $file. Download the standalone C++ SDK for Windows x64."
    }
}

"SOH_DISCORD_SDK_ROOT=$sdkRoot" | Out-File -LiteralPath $env:GITHUB_ENV -Encoding utf8 -Append
Write-Host 'Discord Social SDK checksum verified and Windows x64 files extracted.'
