param([ValidateRange(1, 65535)][int]$Port = 9000)
$ErrorActionPreference = 'Stop'
try {
    $tailscale = Join-Path $env:ProgramFiles 'Tailscale\tailscale.exe'
    if (-not (Test-Path -LiteralPath $tailscale)) { throw 'Install and sign in to Tailscale first.' }
    $addresses = & $tailscale ip -4
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read Tailscale IP. Check sign-in and Tailscale service.' }
    $address = @($addresses | Where-Object { $_ -match '^100\.\d+\.\d+\.\d+$' }) | Select-Object -First 1
    if (-not $address) { throw 'No Tailscale IPv4 found. Connect Tailscale first.' }
    Write-Host "Chat server address: $address  TCP port: $Port"
    Write-Host 'Share this IPv4 and port with invited Tailscale users.'
    Write-Host 'Keep this window open. Type /quit to stop.'
    & (Join-Path $PSScriptRoot 'chat_server.exe') $Port $address
    if ($LASTEXITCODE -ne 0) { throw "Chat server exited with code $LASTEXITCODE" }
} catch {
    Write-Error $_
    exit 1
}
