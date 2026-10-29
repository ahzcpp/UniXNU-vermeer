# 如何从 ios-vermeer-lab 学习（而不是照抄）

`xyyyds0106/ios-vermeer-lab` 是**同一台 Redmi K70** 上走得最远的移植——它出现过苹果图标和进度条，
也有能用的 USB 控制台。所以它的价值极高。

**但它不是 UniXNU。** 两套栈从内核版本到加载器到 ADT 生成方式全都不同，直接拿它的代码或补丁
会失败，而且失败方式往往不明显。本页说明：**哪些能直接用、哪些只能学思路、哪些完全不能碰**，
以及怎么自己验证 lab 的一条说法。

**标注约定**（与本仓库其他文档一致）：
**【已验证】** 作者直接读过对应源码/配置确认 ·
**【未验证】** 来自他人记录，未复核 ·
**【已排除】** 确认不是问题，不要重复排查

---

## 1. 两套栈的差异（这是全部问题的根源）

| 项目 | ios-vermeer-lab | 本仓库（UniXNU） |
| --- | --- | --- |
| XNU 代码base | **xnu-8796**，取自 `blacktop/darwin-xnu-build`，`MACOS_VERSION=13.5`，自述"与 iOS 16.6/16.7 同代" | iPadOS **17.7.11 / 21H461** 的 XNU |
| 目标系统 | iOS **16.7.16 / 20H392**，**HFSX** 根卷 | iPadOS **17.7.11 / 21H461**，APFS |
| 内核产出方式 | **源码构建**，自带 `0001..00xx` 补丁序列 | 源码 XNU + **对真实 kernelcache 预链接**（`XnuPrelink`） |
| machine config | **`SWEET`**（它自己的），代码里到处是 `#if defined(ARM64_BOARD_CONFIG_SWEET)` | **BCM2837**（`mach.development.bcm2837.unstripped.noctf`，见 `board.toml` 的 `[kernel.prelink]`） |
| 加载器 | **自己的裸机 ARM64 Image shim**（`boot/main.c` 自述 "Storage-free handoff to a separately validated vermeer-early kernel"，自带 PSCI 复位、自己解析 FDT） | **UEFI（Mu-Silicium）+ UniXNU + DarwinBoot** |
| ADT 生成 | 自己的 `tools/mkvermeeradt.py` + `tools/adt.py`，自述 "Minimal single-core vermeer ADT" | `scripts/unixnu/devicetree.py`，从 `dts/vermeer.dts` + SoC dtsi 编译生成 |
| 驱动模型 | 直接写进内核的 `xnu/vermeer/*.c`（`vermeer_ufs_block.c`、`vermeer_touch.c` …） | UniXNU **组件目录**里的 IOKit 驱动（`ShengUfs`、`ArmCpu`、`Adreno740` …） |

**【已验证】** 出处：`lab2/xnu/README.md:4-10`、`lab2/README.md:97/177/187`、`lab2/boot/main.c:1`、
`lab2/tools/mkvermeeradt.py:1`、`lab2/xnu/vermeer/vermeer_usb_debug.c:1-10`；
本仓库 `board.toml` 的 `[kernel.prelink]`、`[firmware]`、`[kernel] patch_series`。

### 1.1 这些差异的直接后果

1. **补丁行号必然不对。** 两边 XNU 不同代，同一个文件的同一处上下文行号不同。
2. **`#if defined(SWEET)` 的代码在我们这边编不进去。** 我们的 machine config 是 BCM2837。
3. **依赖 lab 自有设施的代码整块不可用。** 它的 USB 控制台依赖 `vermeer_debug_*` 日志环和它自己的
   早期控制台；我们没有这些符号。
4. **加载器阶段的经验不通用。** lab 的 "no payload in initrd" 是它自己 shim 的问题；我们走
   UEFI + DarwinBoot，完全不同的失败面。
5. **根卷格式不同。** lab 是 HFSX，我们是 APFS，存储阶段的结论不能互换。

---

## 2. 三类分级

### A 类：可直接采用——**硬件事实**

这类是这台手机本身的性质，与用哪套软件栈无关。但**仍然要自己复核一遍**，方法见第 4 节。

| 事实 | lab 的说法 | 我们的独立验证 | 结论 |
| --- | --- | --- | --- |
| 帧缓冲基址与几何 | `0xb8000000` / 1440x3200 / stride 5760 | **【已验证】** 原厂 `uefiplat.cfg` 的 "Display Reserved" 是 `0xb8000000 + 0x2b00000`；本仓库 `dts/vermeer.dts` 一致 | **采用**（已在用） |
| 虚拟定时器 INTID | 27 | **【已验证】** 上游 `ArmCpu.cpp` 本来就硬编码 27（`kSgiPpiEnable = 0xffffU \| (1U << 27)`） | **已自动满足**，无需动作 |
| 启动 CPU 型号 | `arm,cortex-a510` | **【已验证】** 原厂 DTB 的 `/cpus/cpu@0` 与 SoC dtsi 的 `compatible = "arm,cortex-a510"` 一致 | **采用**（已在用） |
| 高位 DRAM 银行 | `0x8c0000000..0x940000000`（2 GiB），32768 个稀疏采样照片确认 | **【已验证】** 本仓库三份静态来源**都看不到它**（原厂 UEFI 图最高 `Conv` 段仅 210 MiB、原厂 DTB `/memory` 是 `0+0`、reserved-memory 全在 4 GiB 以下，见 `scripts/read-stock-layout.py`） | **需自测确认**，见 `docs/playbook.md` 第 2 节 |
| UFS LUN0 末尾 LBA | `0x3afa3ff` | 未独立验证 | 阶段 6 用 `fastboot oem get-partition-info userdata` 实测 |
| 触控器 | Goodix GT9916S，SE4 | 未独立验证（原厂 DTB 可查） | 阶段 7 再处理 |

### B 类：只能借鉴思路——**机制与现象**

代码不能抄，但**它揭示的机制在上游 XNU 里是共有的**，所以现象会复现。

#### B-1 早期 I/O 页表预算（和我们当前这面墙直接相关）

**【已验证】** `arm_vm_init.c` 里那段预算公式**是上游 XNU 的代码**，两代都有：

```c
va_l1_end = va_l1 + ((2 + (mem_segments * 10)) << 20);
va_l1_end += round_page(args->Video.v_height * args->Video.v_rowBytes);
va_l1_end = (va_l1_end + 0x7FFFFF) & ~0x7FFFFFULL;
```

所以"帧缓冲被映射两次会超出预算"这个**现象**会在我们这边同样发生。

**但 lab 的补丁文件不能直接用**，有两个原因：
1. **【已验证】** 它的 hunk 带 `#if defined(SWEET)` 守卫——我们这边这段代码不会被编译进去
2. 行号按 lab 的 xnu-8796 写，我们这代对不上

**正确做法**（已经实践过，见第 3 节）：在我们自己展开的 XNU 源码树里找到那一行，
手工改完用 `git diff` 生成我们这边的补丁，再按 `patches/xnu/series` 流程挂上。

#### B-2 "读写屏幕要走物理 aperture"

**【未验证】** lab 的记录指出：帧缓冲是 no-map 预留区，`arm_vm_init` 跑过之后只能通过物理
aperture 访问。

这个机制是对的，而且**在 UniXNU 生态里有现成的、同 SoC 的实现**——不必从 lab 移植：
`peter8889/UniXNU-ishtar`（小米 13 Ultra，同 SM8550、同面板几何）的 `package/BootProgressMin.c`
用 `extern const uint64_t physmap_base` 加 `at s1e1r` 地址翻译验证来做，**而且它本来就是
UniXNU 的组件模型**。详见 `docs/playbook.md` 第 3 节。

**这是本页最重要的一条结论：当一个机制在 UniXNU 生态里已有同 SoC 的实现时，优先用它，
不要从 lab 移植。**

#### B-3 只读优先的工作纪律

**【已验证】** lab 的每个新文件头部都写着这类话：
"read-only"、"gated"、"default-off"、"never changes GCC, SMMU, PMIC or storage"、
"None is registered yet"、"Reuses ABL's PHY/clocks"。

**这个纪律值得照搬**：先只读观测、默认关闭、不写外设寄存器、确认了再往前。
本仓库 `docs/playbook.md` 的第 0 步（只读 UEFI 探针测窗口）就是同一个路子。

### C 类：完全不能用——**lab 自有栈的代码**

| lab 的东西 | 为什么不能用 |
| --- | --- |
| `xnu/vermeer/vermeer_usb_debug.c` | **【已验证】** 文件头是 `#if defined(ARM64_BOARD_CONFIG_SWEET)`；依赖 `vermeer_debug_*` 日志环、`vermeer_debug_sample_request` 等一整套它自己的设施。我们没有这些符号。想要 USB 日志，走 UniXNU 生态的路（见第 5 节） |
| `xnu/patches/0012-sweet-keep-debug-console-after-ttbr0.patch` | 它修的是 lab 自己的 framebuffer serial console（patch 0003/0010），**UniXNU 没有这个控制台** |
| `xnu/patches/0001..00xx` 整个序列 | 针对 xnu-8796 的源码构建；我们是 21H461 + 预链接，序列和机制都不同 |
| `boot/`（loader、console、fdt、entry.S …） | 它自己的裸机 shim。我们走 UEFI + DarwinBoot |
| `tools/mkvermeeradt.py`、`tools/adt.py` | 它自己的 ADT 生成器。我们用 `devicetree.py` 从 DTS 生成 |
| `xnu/vermeer/vermeer_touch.c`、`vermeer_rpmh_*`、`vermeer_gpi*`、`vermeer_ufs_block.c` 等 | 直接写进内核的驱动，不是 IOKit 组件。UniXNU 侧对应的是组件目录里的驱动 |
| `tools/capture-debug.ps1` | 配套它自己的 USB 协议。UniXNU 自带主机端工具（见第 5 节） |

---

## 3. 已经踩过的坑（真实案例，别重复）

主手曾按 lab 的 `0019-vermeer-early-io-budget.patch` 写了一份补丁交过来，结果：

```
error: corrupt patch at line 39
```

**两个原因**，都属于第 1.1 节列的后果：
1. 补丁文件前面放了说明散文，但没按 git patch 格式组织
2. hunk 行号是照 lab 的 XNU 写的，和我们这代对不上

**正确的做法**（负责本地构建的一方当时就是这么做对的）：
在我们自己的 XNU 工作树里搜 `mem_segments * 10`，在它下面那行
`va_l1_end += round_page(args->Video.v_height * args->Video.v_rowBytes);` 后面**再复制一遍同样这一行**，
然后 `git diff > patches/xnu/zzyg-vermeer-early-io-budget.patch`，把文件名加进 `patches/xnu/series`。

**【已验证】** 该补丁目前**不在本仓库里**：`patches/xnu/` 只有 `zzyd-sheng-audio-addon-mount.patch`、
`zzye-sheng-cpu-idle.patch`、`zzyf-sheng-amp.patch`，`series` 也只列这三个。
它当时只在本地工作树里生成并通过 `git apply`，没有推上来。要可复现就把补丁和 `series` 那一行一起提交。

**教训：从 lab 来的任何补丁，都要在我们自己的源码树里重新生成，不能直接拿文件。**

---

## 4. 怎么验证 lab 的一条说法（标准动作）

拿到 lab 的任何一条结论，先判断它属于哪一类，再按下面做：

**第一步：判类。**
它是在说"这台手机的某个地址/几何/型号"（A 类硬件事实）？
还是在说"XNU 的某个行为"（B 类机制）？还是在说"我们的某个文件应该怎么写"（C 类，基本不可用）？

**第二步：A 类用本仓库自带的原厂数据复核。**

```sh
python3 scripts/read-stock-layout.py
```

**【已验证】** 它只读 `scripts/data/vermeer-uefiplat.cfg`（原厂 UEFI 平台配置，从 OS1.0.x
`uefi.img` 提取，FV 在 `0xa7000000`）和 `scripts/data/vermeer.dtb`（原厂 Android 设备树），
不联网、不碰设备。能直接给出内存图、预留区、外设基址。

需要查别的节点时，改一下脚本里 `fdt_walk()` 的匹配串即可——它是通用的 FDT 遍历器。

**一个真实例子**：lab 相关记录里 GMU 地址有两个说法（有第三方 README 说 `0x3d68000`），
而原厂 DTB 给出的是 `/soc/qcom,gmu@3d69000`。**我们 SoC dtsi 里写的是 sheng 的 `0x3d6a000`。**
三个值都不同，**以原厂 DTB 为准**。详见 `docs/hardware-facts.md` 第 2.3 节。

**第三步：B 类去上游 XNU 源码里确认那段代码我们这代也有。**
展开后的源码树在解析输出目录的 `tree/.cache/xnu-work/`。

**第四步：C 类先在 UniXNU 生态里找同 SoC 的现成实现**（见第 5 节的清单），找不到再考虑移植。

**第五步：凡是只有 lab 单方面说法、我们这边无法复核的，在文档里标【未验证】**，
不要写成结论——本仓库三份文档都遵守这条。

---

## 5. 想要 lab 那样的能力，在 UniXNU 生态里对应什么

| 想要的能力 | 不要从 lab 抄 | UniXNU 生态里的对应物 |
| --- | --- | --- |
| 过了 `arm_vm_init` 还能写屏 | `0012-sweet-keep-debug-console-after-ttbr0.patch` | **【已验证】** `peter8889/UniXNU-ishtar` 的 `package/BootProgressMin.c`（730 行，同 SM8550、同面板几何，走 `physmap_base` + `at s1e1r` 故障安全），配 `Yooglery/UniXNU-sirius` 的门禁放宽做法。见 `docs/playbook.md` 第 3 节 |
| USB 内核日志 | `vermeer_usb_debug.c` + `capture-debug.ps1` | **【已验证】** 主机端上游已自带：`scripts/debug_terminal.py`（34 KB）、`scripts/debug-terminal.cmd`（**Windows CMD**）、`scripts/terminal-requirements.txt`。**但设备侧驱动不在组件目录**（`components.toml` grep `UsbDebug` 为空），且上游明说"这些工具运行在电脑端，不会升级设备内核……修改 USB ID 本身不提供协议兼容性……板级驱动保留在设备仓库"。**【未验证】** 第三方 `FFT-infra/QcomXnuUsbDebug` 据称在 SM8150 上验证过（`XNU_QCOM_USB_DEBUG=1` + `qcom-usb-debug=1`） |
| 自动回 bootloader 重复测试 | `vermeer_restart_control.h`（**【已验证】** 自述 "None is registered yet"，是没有实现注册的策略骨架） | 上游 `docs/zh/porting/log-capture.md` 第 6 节的自动循环；需要阶段 6 存储先通 |
| 持久化日志 | lab 的 USB replay | **【已验证】** 上游 `log-capture.md` 第 4 节指向 sheng 的实现：`package/BootLog.c`、`BootReset.c`、`package/linux-log/`、`scripts/read-sheng-log.py`。需要阶段 6 存储先通 |
| 固件阶段日志 | — | **【已验证】** `[payload] tools = true` + 固件 DSC/FDF 加入 `UniXNUPkg/Driver/AdvancedLogDxe/AdvancedLogDxe.inf`，二维码播出、另一台手机扫。本仓库和 `uefi-vermeer` **都没有**（grep 全空）。**注意它只能看到 `ExitBootServices` 之前** |
| 触摸 | `vermeer_touch.c` + `vermeer_rpmh_*` | 组件目录里的 `ShengNovatekTouch` / `ShengHidInput` / `QcomGeniSpi`；K70 是 Goodix 不是 Novatek，阶段 7 再说 |
| UFS 根卷 | `vermeer_ufs_block.c` | 组件 `ShengUfs`（本仓库 `board.toml` 已选）。阶段 6 |

---

## 6. lab 的更新情况（截至本文写作）

**【已验证】** `main` 分支停在 `d1dcb06`（2026-10-21）。但有 5 个更新的工作分支：

| 分支 | 最新日期 | 内容 |
| --- | --- | --- |
| `k70-functional-touch-power-b9d95a7` | 2026-10-22 | 触摸 SPI/GPI、RPMh 电源、SPMI 重启、IOMMU（242 文件 / +24411 行） |
| `gpu-hfi-deadline-fa2823b` | 2026-10-21 | GPU HFI、RPMh 只读观测、restart probe、媒体/TLMM |
| `gpu-touch-dma-42ce3a7` | 2026-10-21 | 触摸 DMA、基带、蓝牙 H4、GENI DMA |
| `gpu-touch-gpi-dd20b83` / `-e22a0e2` | 2026-10-21 | GPI 诊断 |

**【已验证】** 逐个比对后的结论：**全部是阶段 6/7 的外设工作，对阶段 5 没有可用内容。**
其中 `vermeer_usb_debug.c` 的改动只是加了一个 `usb_ready` 原子标志供触摸 worker 查询，
**不是日志通道本身的改进**；`vermeer_restart_control.h` 没有注册任何实现。

**下次再看 lab 时记得查工作分支**，不要只看 `main`：

```sh
git ls-remote --heads https://github.com/xyyyds0106/ios-vermeer-lab
```

---

## 7. 一句话

**lab 告诉我们这台手机"是什么样"（A 类，采用但要复核）和"XNU 会怎么坏"（B 类，借鉴机制）；
它的代码和补丁告诉我们"它那套栈怎么修"，那个对我们无效（C 类）。
遇到 C 类需求，先在 UniXNU 生态里找同 SoC 的现成实现，再考虑移植。**
