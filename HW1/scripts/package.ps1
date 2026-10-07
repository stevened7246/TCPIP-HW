param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build'),
    [string]$ToolchainBin = 'D:\msys2\ucrt64\bin'
)
$ErrorActionPreference = 'Stop'
# 將已編譯程式、必要 DLL、使用說明與授權整理成用戶端及伺服器 ZIP。
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$objdump = Join-Path $ToolchainBin 'objdump.exe'
if (-not (Test-Path -LiteralPath $objdump)) { throw "Missing objdump: $objdump" }
# Unicode filename without depending on Windows PowerShell's source encoding.
$manual = Join-Path $root (([string][char]0x8AAA) + [char]0x660E + [char]0x66F8 + '.md')
if (-not (Test-Path -LiteralPath $manual)) { throw 'User manual is missing.' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$destination = Join-Path $build "packages\$stamp"
New-Item -ItemType Directory -Path $destination | Out-Null

function Copy-RuntimeDependencies([string]$Binary, [string]$Folder) {
    # 遞迴檢查 DLL 相依，只複製工具鏈的執行期 DLL，系統 DLL 保留由 Windows 提供。
    $pending = [System.Collections.Generic.Queue[string]]::new()
    $seen = @{}
    $pending.Enqueue($Binary)
    # queue 追蹤仍待檢查的 EXE/DLL，seen 避免重複複製或相依循環。
    while ($pending.Count -gt 0) {
        $current = $pending.Dequeue()
        $imports = & $objdump -p $current
        if ($LASTEXITCODE -ne 0) { throw "Cannot inspect $current" }
        foreach ($line in $imports) {
            if ($line -notmatch 'DLL Name:\s*(\S+)') { continue }
            $dll = $Matches[1]
            if ($seen.ContainsKey($dll)) { continue }
            $seen[$dll] = $true
            $source = Join-Path $ToolchainBin $dll
            if (Test-Path -LiteralPath $source) {
                Copy-Item -LiteralPath $source -Destination $Folder
                $pending.Enqueue($source)
            } elseif ($dll -notlike 'api-ms-win-*' -and
                      -not (Test-Path -LiteralPath (Join-Path "$env:WINDIR\System32" $dll))) {
                throw "Unresolved runtime dependency: $dll"
            }
        }
    }
}

foreach ($kind in @('client', 'server')) {
    # 用戶端只包含 GUI；伺服器另附啟動腳本，兩者各自帶必要 DLL。
    $folder = Join-Path $destination "nightlink-$kind"
    New-Item -ItemType Directory -Path $folder | Out-Null
    if ($kind -eq 'client') {
        $binary = Join-Path $build 'gui\chat_gui.exe'
    } else {
        $binary = Join-Path $build 'chat_server.exe'
    }
    Copy-Item -LiteralPath $binary -Destination $folder
    Copy-RuntimeDependencies $binary $folder
    Copy-Item -LiteralPath $manual -Destination $folder
    if ($kind -eq 'server') {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'start-server.ps1') -Destination $folder
        @'
@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-server.ps1"
pause
'@ | Set-Content -LiteralPath (Join-Path $folder 'start-server.cmd') -Encoding ASCII
    }
    $licenseRoot = Join-Path (Split-Path $ToolchainBin -Parent) 'share\licenses'
    $licenses = Join-Path $folder 'licenses'
    New-Item -ItemType Directory -Path $licenses | Out-Null
    foreach ($package in @('gcc-libs', 'winpthreads', 'libwinpthread')) {
        $notice = Join-Path $licenseRoot $package
        if (Test-Path -LiteralPath $notice) { Copy-Item -LiteralPath $notice -Destination $licenses -Recurse }
    }
    $manifest = Get-ChildItem -LiteralPath $folder -File | ForEach-Object {
        # 為包內頂層檔案計算 SHA-256，方便收件者檢查內容是否完整。
        '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash, $_.Name
    }
    $manifest | Set-Content -LiteralPath (Join-Path $folder 'SHA256SUMS.txt') -Encoding UTF8
    Compress-Archive -LiteralPath $folder -DestinationPath (Join-Path $destination "nightlink-$kind.zip")
}
Write-Output $destination
