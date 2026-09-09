# Hex Spire —— 编译 + 验证一键脚本
#
# 用法：
#   .\Tools\Verify.ps1              # 编译 + 跑全部验证套件
#   .\Tools\Verify.ps1 -Suite hex   # 只跑指定套件
#   .\Tools\Verify.ps1 -NoBuild     # 跳过编译，直接跑验证
#
# 退出码：0 = 全部通过，非 0 = 存在失败

param(
    [string]$Suite = "all",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$UProject    = Join-Path $ProjectRoot "HexSpire.uproject"
$UE          = "C:\Program Files\Epic Games\UE_5.8"
$BuildBat    = Join-Path $UE "Engine\Build\BatchFiles\Build.bat"
$EditorCmd   = Join-Path $UE "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$LogFile     = Join-Path $ProjectRoot "Saved\Logs\verify.log"

# ─────────────────────────────── 编译
if (-not $NoBuild) {
    Write-Host "[1/2] 编译 HexSpireEditor ..." -ForegroundColor Cyan

    $buildOutput = & $BuildBat HexSpireEditor Win64 Development -Project="$UProject" -WaitMutex 2>&1
    $buildOk = $LASTEXITCODE -eq 0 -and ($buildOutput | Select-String -Quiet "Result: Succeeded")

    if (-not $buildOk) {
        Write-Host "编译失败：" -ForegroundColor Red
        $buildOutput | Select-String -Pattern "error|Error:" | Select-Object -First 30 |
            ForEach-Object { Write-Host "  $($_.Line)" -ForegroundColor Red }
        exit 1
    }
    Write-Host "  编译通过" -ForegroundColor Green
}

# ─────────────────────────────── 验证
Write-Host "[2/2] 运行验证套件 (suite=$Suite) ..." -ForegroundColor Cyan

& $EditorCmd "$UProject" -run=HexVerify -suite=$Suite `
    -unattended -nopause -nosplash -AbsLog="$LogFile" 2>&1 | Out-Null

$verifyExit = $LASTEXITCODE

if (Test-Path $LogFile) {
    Select-String -Path $LogFile -Pattern "LogHexSpire" | ForEach-Object {
        $line = ($_.Line -split '\]', 3)[-1] -replace '^LogHexSpire(Core)?: ?(Display: )?', ''
        if ($line -match '\[失败\]|✗|FAIL') {
            Write-Host $line -ForegroundColor Red
        }
        elseif ($line -match '全部通过|PASS|★') {
            Write-Host $line -ForegroundColor Green
        }
        elseif ($line -match '^──|^═══') {
            Write-Host $line -ForegroundColor DarkGray
        }
        else {
            Write-Host $line
        }
    }
}
else {
    Write-Host "未找到日志文件: $LogFile" -ForegroundColor Red
    exit 1
}

exit $verifyExit
