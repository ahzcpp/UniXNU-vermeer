# K70 引导内 USB 日志研发记录（2026-11-04）

源码和离线测试已完成；源码补丁已提交到本研究分支。完整固件的最终构建、FD 审计和真机 USB 验证须分别记录，不能用源码或编译结果代替 USB/XNU/iOS 成功证据。

## 获取与构建源码

[源码补丁](../uefi-usb-diagnostics.patch) 与 [哈希清单](../uefi-usb-diagnostics.manifest.json) 已在 [284011c](https://github.com/ahzcpp/UniXNU-vermeer/commit/284011c6e40fcc80f5d7f3a2b4705abff5a918a0) 提交，包含 75 个文本文件。它未包含 OEM 或 Apple 二进制，也未改变默认内核工作流。

在本仓库根目录检查并应用：

```sh
git apply --check uefi-usb-diagnostics.patch
git apply uefi-usb-diagnostics.patch
```

随后阅读 `package/uefi/K70DiagnosticsPkg/Tools/README.md` 与 `README-build-firmware.md`。`prepare-oem.py` 仅接受下面固定 SHA-256 的原始 `uefi.img`，逐项验证 30 个所选模块后生成独立 OEM 输入；`build-firmware.py` 使用已装配好的固定 SDK 构建单独的诊断 profile。这些脚本没有刷写或 USB 操作。

## 本轮目标与手机状态

用户明确要求在现有 UniXNU/UEFI 引导中对接 USB、直接读取设备信息和日志，减少依赖拍照。此前 Linux reader 的 boot/vendor_boot 组合虽完成写入，但手机停留 Redmi/小米 Logo，未枚举预期 USB、未取得 snapshot；随后已恢复原厂 vendor_boot_b 与此前稳定的 trace02 UEFI 引导。reader 失败不能当成日志采集成功。本方案继续沿 UEFI 方向研究。

当前诊断 profile 的目标是：停留在 UEFI 菜单期间，通过 Qualcomm USB device provider 提供只读 Fastboot 查询，在 ExitBootServices（EBS）之前取得当前会话的共享日志。它需要真实的 OEM USB 控制器、时钟、电源及其他依赖，应用层本身不能补出这些 provider。

最新 profile 固定 `K70_USB_DIAG_AUTOSTART=0`，关闭 ReadyToBoot 定时服务。受限应用在 UiInit 完成后，于 TPL_APPLICATION 查找唯一匹配的诊断 plugin，并且只调用一次 Run；结束后留在菜单等待真实按键。该 profile 不包含 FAT、ramdisk、系统 loader 或磁盘驱动，不能据此启动 iOS。

原用户最初提供的两张图片仅供参考，不是这台手机的本轮测试证据；旧仓库继承的启动结果也不能替代本机实测。当前手机的既有引导观测见 [handoff-trace-2026-11-04.md](handoff-trace-2026-11-04.md)。

## USB 查询与 Windows 采集

新 consumer 使用 VID `18d1`、PID `d00d`、接口 `ff/42/03`，bulk OUT `01`、IN `81`。固定字符串 `K70UEFI20261104` 是这个诊断实现的**合成序列号**，不是手机硬件序列号。

固件只接受白名单 `getvar` 查询，包括产品、协议版本、固定 HELLO 和日志。它没有刷写、擦除、下载镜像、重启或启动操作系统的命令路径。PC 端 [capture-k70-uefi.ps1](../scripts/capture-k70-uefi.ps1) 固定调用 System32 Fastboot，仅发送 `devices` 及 product/version/k70-hello/k70-log 查询；每轮核对目标 USB 身份，给进程设置超时与输出上限，并分别保留原始 stdout/stderr 和结果报告。

这使用 Fastboot USB 接口。Windows 仍需正确的 WinUSB/兼容 Fastboot 驱动绑定；PnP 中出现设备，并不证明 Fastboot 已能访问它。Microsoft 的 [WinUSB 安装文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/winusb-installation) 说明了自动绑定和手动绑定条件。本机诊断描述符与驱动组合尚未实测。

Qualcomm 的 [EFIUsbDevice.h](https://github.com/yyx2025/uefi-vermeer/blob/ae57e93f0eb6b556f1bb964ffb12f0e78aaaf33c/Silicon/Qualcomm/QcomPkg/Include/Protocol/EFIUsbDevice.h) 是所消费 ABI 的来源。初始化事件、描述符及轮询事件的参考来自 HakiOS 作者的 [xnuloader 源码](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/xnuloader/loader.c)。参考代码和协议存在本身不证明 K70 硬件兼容。

## 共享 RAM 日志及覆盖范围

`K70LogRingDxe` 从实际 MemoryMapLib 的唯一 `DXE Heap` 区域申请总计 64 KiB，随后通过 EFI configuration table 发布共享环。分配必须整个落在该区域内，没有固定物理地址兜底。扣除 80 字节头部后，数据容量为 65,456 字节；满环保留最新内容，并统计覆盖、截断和争用丢弃。

`DxeLogSerialPortLib` 在各 DXE/UEFI 模块内只缓存 SystemTable/共享环指针，先追加日志，再调用原屏幕渲染。它不为每个 PE 建立独立环。写路径不分配内存、不查找协议、不递归打印；争用直接丢弃并计数。快照按时间顺序复制，忙时返回可重试状态。USB 日志查询把静态快照分为 INFO 文本输出，控制字符会清理，因此 PC 文件是带 Fastboot 装饰的文本记录，不能声称是原环字节的无损导出。

覆盖范围必须保留以下限制：

- 只收集实际路由到该 SerialPortLib 的输出；被 DEBUG 级别过滤的消息无法恢复。
- 环安装前的 DXE 输出、SEC/PEI 早期输出和 DXE Core 原串口输出未被这一环收集。
- OEM 二进制自带的日志实现未统一接入，不能声称所有厂商日志均已捕获。
- 数据只属于本次 Boot Services 会话，不是磁盘日志或跨重启持久日志。
- EBS 后停止读取环及调用 USB provider；本方案不提供 XNU/iOS 阶段的 USB 通道。

正常退出应 Abort、等待有限的终态通知、确认缓冲区不再被 DMA 使用，再释放并 Stop。未确认的缓冲区保留，服务禁止复用。EBS 回调只作兜底，无法否决失败的 Stop，也不能证明 DMA 已静默；诊断 profile 应停在菜单，不应把自动启动 consumer 当成 OS 启动屏障。

## OEM 输入与 PEI 前置检查

本地提取使用 vermeer 中国版 `OS2.0.218.0.VNKCNXM`。来源链为 [Firmware Updater 对应发布](https://github.com/XiaomiFirmwareUpdaterReleases/firmware_xiaomi_vermeer/releases/tag/stable-21.10.2026) 及其指向的 [小米原始 OTA](https://cdnorg.d.miui.com/OS2.0.218.0.VNKCNXM/vermeer-ota_full-OS2.0.218.0.VNKCNXM-user-15.0-d894a157b4.zip)。只下载了小 firmware 包，并核对发布资产 digest、文件 SHA-256 和 ZIP CRC；没有执行包内刷写脚本或刷入底层固件分区。

| 本地输入 | SHA-256 |
| --- | --- |
| firmware ZIP，176,121,189 字节 | `0ad27e0d0fb0bc38c720663257847e75784d8d7d00cd8e6c693bcacfe948c617` |
| 原始 uefi.img，2,945,024 字节 | `30501ff03e3a79bc8785ec767bfff53e39290caf15eb401c15836afb0759276b` |
| 解出的 OEM SEC TE | `06ca85d1d987767af6c0877c6890dc0bf38ae0e560150e50c2b259c51b76b40a` |

这些校验绑定了离线分析输入，**不证明手机前一阶段当前驻留的旧 XBL/SEC 就是同一版本**。链接地址也不是可直接复用的运行时物理地址。

OEM Env/SPMI 的构造过程需要真实 Scheduler。独立的 `K70PlatformPeiLib` 副本使用有界扫描与保留内存白名单：验证 ADRP/ADD 来源寄存器、GUID 地址、RET 边界和候选唯一性，再检查子表及函数地址。实际 OEM SEC TE 原始字节中的版本是 `0x1000B`（1.11）；代码将已知前缀、producer、GUID 和非空方法绑定到同一 TE 的相对模板关系，允许原模板中已证实的 Spinlock/MpCpu 空槽，非空替代仍须指向保留代码，WDog/Lock 方法必须有效。[EFIKernelInterface.h](https://github.com/yyx2025/uefi-vermeer/blob/ae57e93f0eb6b556f1bb964ffb12f0e78aaaf33c/Silicon/Qualcomm/QcomPkg/Include/Protocol/EFIKernelInterface.h) 只是较老的已知前缀参照，不能当作 1.11 版本来源。没有伪造 Scheduler 协议或猜测地址。

匹配 OEM SEC TE 的 DTB 扩展表为 `0x108` 字节、版本 `0x10001` 加 32 个函数指针。代码校验同一 TE 的相对重定位关系、producer 与 GUID 的相对位置，以及已证实的运行时 DTB 指针。仅在声明的保留区内核对 FDT 大小、边界及 `/sw/boot/config`。静态模板存在不足以代表原初始化成功；不满足检查则在 PEI 打印明确原因并返回错误，由 SEC 阻止 DXE 派发。

FV-list 保留原厂 592 字节布局，修复了 `UINTN *` 被当成字节指针所造成的越界，并增加锁方法检查与失败清理。上述验证只能确认有限的结构、映射和输入一致性，不能证明继承 Scheduler 的栈、定时器、中断、安全权限及其他运行时状态仍可工作。

## 已完成与待验证

| 项目 | 本文记录状态 |
| --- | --- |
| 环回、截断、争用、快照及原屏幕包装层 host 测试 | 已通过 ASan/UBSan 和参考模型 |
| USB 命令白名单、异步完成、超时和只读日志流 mock | 已通过；未访问手机 |
| 有界 scanner、真实 bundled libfdt、FV-list 布局和失败清理测试 | 已通过离线测试；未执行 OEM 代码 |
| 新模块 AArch64 编译及模块级 EDK2 链接 | 已完成对应检查 |
| final03 完整固件构建及源哈希核对 | EDK exit 0；冻结源文件未变化 |
| 最终 FD 解包、OEM PE/DEPEX、SEC TE 与当前构建输出绑定 | 通过；30 OEM PE/原 DEPEX 和 39 源码 PE 对应，SEC TE 重定位与源码身份对应 |
| 完整 Android boot 镜像打包与独立解包 | 通过；1,699,840 字节、v0、空 ramdisk，无 FAT/XNU/系统根卷 |
| boot_b 真机刷写及重启 | 均返回 OKAY；没有写 vendor_boot 或 userdata |
| K70 枚举、HELLO、真实日志回传 | 61.9 秒、15 次成功的有界主机枚举查询内未发现目标身份，没有收到 HELLO 或日志 |

完整 boot 镜像 SHA-256 为 `9d83f4aa8a17cff018b69d54ed81920486808387bc011c8d79e8c617b9150979`；3 MiB FD 为 `b723a290ae884af6e5f451a8149b54aad53abc9a897d034efcd9fcf0c5c43c75`。已检查 Ring 与 USBdiag 各一份，FAT、DiskIo、Partition、UFS、SDCC、MSD、XHCI、DarwinBoot 和 UFPLoader 均未进入最终 FD。摘要见 [离线验证清单](uefi-usb-verification-2026-11-04.json)。

本诊断镜像已写入 boot_b，原厂 vendor_boot_b 保持恢复后的内容。刷写成功只证明 Fastboot 接受了镜像；当前手机屏幕状态未确认，未获得固件执行到哪一阶段的证据。用户随后要求暂停 USB 检测，继续 XNU 与系统启动工作。没有继续重试 USB，也没有把编译通过、日志模板或其他设备参考提升为本机 USB/iOS 成功证据。
