# K70 r33s 临时启动脚本（给小白）
# 作用：用 fastboot 把本文件夹里的 boot.img 临时启动起来，不刷任何分区。
# 重启手机就回到原来的系统（Android / 原 boot），底层分区完全没动。
#
# 用法：手机进 fastboot（关机后按住 音量下 + 电源，或 `adb reboot bootloader`），
#       数据线连电脑，右键本文件“使用 PowerShell 运行”，或在 PowerShell 里执行：
#            powershell -ExecutionPolicy Bypass -File .\临时启动.ps1
#
# 需要：电脑装好 fastboot（platform-tools）。手机 bootloader 已解锁。

$ErrorActionPreference = 'Stop'
$here  = Split-Path -Parent $MyInvocation.MyCommand.Path
$img   = Join-Path $here 'boot.img'
$want  = '0fc56179ce9d41f31ad276f057890dbab32dafbdb16ceed4c18589c8016c74d1'   # r33s 成品哈希

if (-not (Test-Path $img)) { throw "找不到 boot.img，请确认它和本脚本在同一个文件夹" }

# 找 fastboot
$fb = (Get-Command fastboot -ErrorAction SilentlyContinue).Source
if (-not $fb) { $fb = 'C:\Windows\System32\fastboot.exe' }
if (-not (Test-Path $fb)) { throw "找不到 fastboot，请先安装 Android platform-tools 并加入 PATH" }

# 校验 boot.img 没被改坏
$have = (Get-FileHash -LiteralPath $img -Algorithm SHA256).Hash.ToLower()
if ($have -ne $want) { throw "boot.img 哈希不对（$have），文件可能损坏，请重新拷贝" }
Write-Host "boot.img 校验通过 ($have)" -ForegroundColor Green

# 认设备
$devs = @(& $fb devices 2>&1 | ForEach-Object { $_.ToString() } | Where-Object { $_ -match '^\S+\s+fastboot\s*$' })
if ($devs.Count -eq 0) { throw "没检测到 fastboot 设备。请让手机进入 fastboot 模式并插好数据线" }
if ($devs.Count -gt 1) { throw "检测到多台设备，请只连一台" }
$serial = ($devs[0] -split '\s+')[0]
$product = (& $fb -s $serial getvar product 2>&1) -join "`n"
if ($product -notmatch 'vermeer') {
  Write-Host "警告：这台设备 product 不是 vermeer（K70）。继续请按回车，否则 Ctrl+C 退出。" -ForegroundColor Yellow
  Read-Host
}

Write-Host "正在临时启动（fastboot boot，不写分区）……" -ForegroundColor Cyan
& $fb -s $serial boot $img
if ($LASTEXITCODE -ne 0) { throw "fastboot boot 失败" }
Write-Host ""
Write-Host "已从内存启动。手机屏幕应先出现苹果 logo / 底部状态条，约 1~3 分钟后到锁屏（简体中文）。" -ForegroundColor Green
Write-Host "重启手机即可回到原系统。" -ForegroundColor Green
