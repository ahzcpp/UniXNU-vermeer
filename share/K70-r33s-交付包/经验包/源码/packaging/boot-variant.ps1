# Temporary boot (fastboot boot, no partition write) of artifacts\storage-native-09-<Variant>, then a
# short COM reader. boot_b keeps r4 and userdata keeps the r4 root; a normal reboot returns to r4.
param([Parameter(Mandatory=$true)][string]$Variant, [Parameter(Mandatory=$true)][string]$ExpectedImageSha256, [int]$ComWaitSeconds = 300)
$ErrorActionPreference = 'Continue'
$k70Root = 'E:\K70-UniXNU-20261104'
$k70Artifact = Join-Path $k70Root ('artifacts\storage-native-09-' + $Variant)
$k70Image = Join-Path $k70Artifact 'boot.img'
$k70Proof = Get-Content -LiteralPath (Join-Path $k70Artifact 'verification.json') -Raw | ConvertFrom-Json
if ($k70Proof.schema -ne ('k70-storage-fullboot-offline-' + $Variant) -or -not $k70Proof.offline_container_verified -or -not $k70Proof.fd_bytes_read_back_equal -or -not $k70Proof.fat_payload_read_back_equal -or $k70Proof.required_owned_window_bytes -ne 2147483648) { throw "Incomplete $Variant verification" }
if ($k70Proof.fd_sha256 -ne '0827f6df15ab79857066c60e2ea11abd4a8cbf63a08de8912b188f212c98ef45') { throw 'Must reuse the r5 FD' }
if ($k70Proof.image_sha256 -ne $ExpectedImageSha256 -or (Get-FileHash -LiteralPath $k70Image -Algorithm SHA256).Hash.ToLowerInvariant() -ne $ExpectedImageSha256) { throw 'Boot image identity mismatch' }
$k70Fastboot = 'C:\Windows\System32\fastboot.exe'
# 序列号已移除：自动认出唯一一台处于 fastboot 模式的设备。多台设备时请自行用 -s 指定。
$k70Devices = @(& $k70Fastboot devices 2>&1 | ForEach-Object { $_.ToString() } | Where-Object { $_ -match '^\S+\s+fastboot\s*$' })
if ($k70Devices.Count -ne 1) { throw 'fastboot 下必须且只能有一台设备' }
$k70Serial = ($k70Devices[0] -split '\s+')[0]
$k70Text = (& $k70Fastboot -s $k70Serial getvar product 2>&1 | ForEach-Object { $_.ToString() }) -join "`n"
if ($k70Text -notmatch '(?m)product:\s*vermeer') { throw 'Not vermeer' }
$k70Log = Join-Path $k70Root ('device-session\boot-' + $Variant + '-temp-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.txt')
"K70 $Variant temporary boot (fastboot boot, no partition write)`nImage SHA256: $ExpectedImageSha256`nKernel SHA256: $($k70Proof.payload_kernel_sha256)`nArgs: $($k70Proof.boot_arguments)" | Out-File -LiteralPath $k70Log -Encoding utf8
& $k70Fastboot -s $k70Serial boot $k70Image 2>&1 | ForEach-Object { $_.ToString() } | Tee-Object -FilePath $k70Log -Append
if ($LASTEXITCODE -ne 0) { throw 'fastboot boot failed' }
Write-Host "Booted $Variant from RAM; starting COM reader."
& (Join-Path $k70Root 'usb-next\read-k70-com.ps1') -WaitSeconds $ComWaitSeconds
