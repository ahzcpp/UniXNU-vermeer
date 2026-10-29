# vermeer 执行手册（给负责本地构建与刷机的一方）

本页是**可直接照做**的操作清单。背景与依据在
[`docs/bring-up.md`](bring-up.md)，硬件实测值在
[`docs/hardware-facts.md`](hardware-facts.md)。

**标注约定**（三份文档一致）：
- **【已验证】** 作者直接读过对应源码/配置确认，带 `文件:行号`，可当事实用
- **【未验证】** 来自他人记录/README/检索，未在本仓库或上游源码复核，**先自己确认再用**
- **【已排除】** 曾怀疑、已确认不是问题，**不要重复排查**

---

## 第 0 节　现在在哪一阶段

官方阶段划分见上游 `docs/zh/porting/bring-up.md`。本机状态：

| 阶段 | 状态 |
| --- | --- |
| 1 UEFI | **通过**（固件启动，画面稳定） |
| 2 UniXNU | **通过**（菜单出现） |
| 3 负载 | **通过**（读到 `unixnu.toml` 与负载） |
| 4 DarwinBoot | **通过**（绿色交接条出现） |
| 5 内核入口 | **卡在这里**：蓝条、品红条出现；`entry_probe_reset_after = "_arm_vm_init"` 的定时复位**会触发**，之后屏幕静止、无文字 |
| 6 存储与根卷 | 未开始 |
| 7 用户态 | 未开始 |

**苹果图标属于阶段 7**，由用户态显示服务绘制，不是引导期任何东西画的。
依据与完整链条见 `docs/bring-up.md` 第 9 节。**不要把它当近期目标。**

---

## 第 1 节　最省时间的一条机制：先看要不要重编内核

**【已验证】** `scripts/unixnu/payload.py:127-143`：入口探针是在 **payload 阶段**装的——
先 `shutil.copyfile(kernel, payloads/'kernel.macho')`，**再**对那个副本跑
`patch-xnu-entry-probe.py`；ADT 也在 payload 阶段由 `devicetree.generate()` 生成。

| 改了什么 | 要跑什么 | 耗时 |
| --- | --- | --- |
| `panic_screen`、任何 `entry_probe_*` | `./unixnu payload` + `./unixnu bootimg` | 分钟级 |
| `owned_memory_size`、`dram_size`、`[payload] kernel_args` | 同上 | 分钟级 |
| `dts/vermeer.dts`、ADT 相关 | 同上 | 分钟级 |
| `configs/*_defconfig`、`patches/xnu/*`、`[kernel] archives` | `./unixnu kernel` 之后再 payload + bootimg | 30–60 分钟 |
| 固件 DSC/FDF、UEFI 源码 | `./unixnu firmware` 之后再 payload + bootimg | 看固件 |

**一次内核构建可以出很多个 boot 镜像。** 一轮里把多个 payload 变体都生成好，连着刷，别一次改一样就重编内核。

**【已验证】** 上游 `docs/zh/xnu/boot-arguments.md#apply-arguments` 的核对方法：

```sh
./unixnu payload --kernel-arg serial=3 --kernel-arg debug=0x14e
./unixnu bootimg
jq -r .kernel_args out/<device>/<os_profile>/<config>/payload/payload.json
mtype -i out/<device>/<os_profile>/<config>/payload/payload.img ::/EFI/UniXNU/unixnu.toml
```

负载命令会输出 `kernel args:`，DarwinBoot 交接前输出 `DARWINBOOT: command line:`。
**两者不同 → 检查选中的卷和启动项；两者相同但行为没变 → 内核或驱动没实现该参数。**

---

## 第 2 节　第 0 步：先实测，再谈窗口（零风险，不用编译）

### 2.1 为什么这步不能跳

**【已验证】** 用 `python3 scripts/read-stock-layout.py` 可自行复核：本仓库里三份静态来源
**一致地**看不到高位 DRAM 银行——

1. 原厂 UEFI 图最大 `EfiConventionalMemory` 段只有 **209.8 MiB**（DXE Heap `0xbab00000`）
2. 原厂 DTB 的 `/memory` 是 **`0 + 0` 占位符**（高通惯例，XBL/ABL 运行时从 SMEM 填）
3. 原厂 DTB 的 reserved-memory **全部低于 4 GiB**（最高 `0xffe00000`）

**所以"这台机器能给多大窗口"在本仓库里无解，只能在设备上测。**

### 2.2 怎么测

**【已验证】** `qusno1/vermeer-probe` 是专为 vermeer 写的**只读** UEFI 探针。它
**不调用 `ExitBootServices`、不写外设寄存器、不读写磁盘、不碰任何分区**，只向帧缓冲输出，
用 GitHub Actions 构建、`fastboot boot` 临时加载。

> 该仓库 README 提醒：K70 是 `vermeer`，K70 **Pro** 是 `manet`，**不能混用**。

屏幕会打印并需要拍照记录的字段：

| 字段 | 用途 |
| --- | --- |
| `WIN BASE` / `WIN LEN` | 最大连续空闲窗口。**算法与 DarwinBoot 一致**（取最大连续 `EfiConventionalMemory` 段，2 MiB 对齐后加 `0x4000`）→ 这就是 `owned_memory_size` 的实测上限 |
| `RAM TOP` / `RAM MB` | 最高连续常规内存顶端 → `dram_size` 的参考 |
| `GOP FB` / `GOP SZ` | 帧缓冲基址/大小，预期 `0xb8000000` |
| `GOP RES` / `GOP STR` / `GOP FMT` | 分辨率/每行像素数/像素格式，预期 `1440x3200`、`1440` |
| `EL` | 异常级别，预期 EL1 |
| `MIDR` / `MPIDR` / `AFF` | CPU 标识与启动核 affinity |
| `CNTFRQ` | 通用定时器频率，**必须是 `19200000`** |
| `MMFR0` / `PFR0` | 特性寄存器，交叉检查 |

### 2.3 拿到数之后

- `CNTFRQ` ≠ `19200000` → SoC dtsi 的 `timebase-frequency` 与实机不符，**先停下来告诉主手**
- `GOP RES`/`GOP STR` 与 `1440x3200`/`1440` 不符 → 帧缓冲几何假设要改，影响所有屏幕输出
- `WIN LEN` < 当前 `owned_memory_size`（`0x20000000`）→ 窗口请求过大，DarwinBoot 会 `NO WINDOW`
- `WIN LEN` 远大于 `0x20000000` → 记下来，**但现在不要动窗口**（理由见第 2.4 节）

### 2.4 为什么现在不动窗口

**【已验证，并更正先前说法】** 先前说过"512 MiB 根本不够跑 iPadOS"，**此条撤回**：其依据
（asteroids 记录里 "384 MiB of owned memory left iPadOS short"）检索未能找到出处，属**【未验证】**。
反向数据点三个、彼此独立（均**【未验证】**）：uke 以 256 MiB 跑到 root-device 等待；
zeta 以 `0x18000000`（384 MiB）通过同一 BSD 检查点；tb128fu 以 `0x80000000`（2 GiB）跑到
iPadOS 17.7.11 主屏。

**结论：512 MiB 对阶段 5 够用。窗口是阶段 6/7 的事。现在改它只会同时引入新变量。**

---

## 第 3 节　第 1 步：把失败变成可见（本阶段的真正解）

### 3.1 为什么现在什么都看不到

**【已验证】** 上游 `scripts/xnu-panic-screen.c:218-250`（`--panic-screen` 把 `_panic` 换成跳到这里）：

```c
volatile struct vc_info *v = (volatile struct vc_info *)VINFO;
g.base = (u8 *)v->v_baseaddr;        /* 裸地址 */
...
for (row = g.y; row < g.h; row++)     /* 先把整屏清成黑 */
  for (col = 0; col < g.w; col++)
    *(volatile u32 *)(g.base + row*g.rb + col*4) = 0;
```

视频控制台建立之前 `v_baseaddr` 是 `0xb8000000` 这个**物理/恒等地址**；而 `arm_vm_init`
已经换掉恒等映射，**清屏循环第一个 store 就自己 data abort**，嵌套异常 → 一个字都没有。

**所以这个阶段任何 panic 都是静默的；我们看到的"卡死"可能本来就是 panic。**
在可见通道做出来之前，逐指令二分测到的是这个级联，不是内核真实停点。

### 3.2 两个零件必须配套做

**(a) 放宽 boot-progress 门禁**

**【已验证】** 上游 `scripts/build-xnu-4k-baseline.sh:78-87`：

```sh
if [ "$BOOT_PROGRESS" = 1 ]; then
  if [ "${XNU_NATIVE_UFS:-0}" != 1 ] || [ "${XNU_OWNER_ROOT_AUDIT:-0}" != 1 ] || \
     [ "${XNU_NATIVE_VIRTIO_BLOCK:-0}" != 1 ] || [ "${XNU_SOURCE_CORECRYPTO14:-0}" != 1 ]; then
    echo 'Boot progress requires the audited UFS/owner-root, Virtio and source-corecrypto build' >&2
    exit 1
  fi
  python3 scripts/build-boot-progress.py
fi
```

其中 `OWNER_ROOT_AUDIT` 与 `NATIVE_VIRTIO_BLOCK` 需要 live-device GDB 观测产物
（见 `configs/vermeer_defconfig` 顶部注释），干净检出造不出来。

**【已验证】** `Yooglery/UniXNU-sirius` 的做法：把上游脚本**整份**复制到设备仓库 `scripts/`，
只改这一处互锁，并写明板级偏离：

```sh
if [ "$BOOT_PROGRESS" = 1 ]; then
  # BOARD DEVIATION from the UniXNU script (keep in sync when re-basing): ...
  # Only the source crypto provider is required here.
  if [ "${XNU_SOURCE_CORECRYPTO14:-0}" != 1 ]; then
    echo 'Boot progress requires the source-corecrypto build' >&2
    exit 1
  fi
  python3 scripts/build-boot-progress.py
fi
```

机制可行：**【已验证】** `scripts/unixnu/tree.py` 的 `_copy_scripts` 按 base → soc → device
顺序复制，**设备脚本覆盖同名上游脚本**。本仓库 `CONFIG_XNU_SOURCE_CORECRYPTO14=y` 已满足放宽后的条件。

> **复制上游脚本是有维护成本的**：上游该脚本更新时必须重新基线化。所以注释里务必写明
> "BOARD DEVIATION … keep in sync when re-basing"，和 sirius 一样。

**【未验证】** sirius 自称该面板在真机上跑出了实时内核日志。但 sirius 是 **SDM710 的 eMMC 板**，
它"UFS/owner-root/Virtio 集成点编译为空"的理由对 SM8550 + UFS 的本机**是否同样成立，需要自己确认**——
这正是必须配套做 (b) 的原因。

**(b) 换一个自包含的观察者**

**不能**直接用本仓库现有的 `package/BootProgress.c`。

**【已验证】** `peter8889/UniXNU-ishtar` 的 `scripts/build-boot-progress-min.py` docstring：
完整版"要读 UFS、Virtio block、ADSP、触摸、GPU 和 cpufreq 的计数器，**精简配置会在约 118 个
未定义符号上链接失败**"；`BootProgressMin.c` 用"**内核镜像之外什么都不依赖**"的方式实现了
同样的十三个入口点。该脚本还会在结尾做未定义符号检查，把这类失败挡在归档阶段而不是内核链接阶段。

**【已验证】** `BootProgressMin.c`（730 行）的关键机制：

```c
/* arm_vm_init() installs the physical aperture; until then the variable is zero. */
extern const uint64_t physmap_base;
...
/* arm_vm_init() replaced the identity map, so the physical address is reachable only
   [through the aperture] */
uintptr_t aperture = (uintptr_t) physmap_base;
```

写屏之前用地址翻译指令确认该地址已映射，**故障安全、不会自己崩**：

```c
__asm__ volatile ("at s1e1r, %1; isb; mrs %0, par_el1" : "=r" (par) : "r" (address) : "memory");
```

几何从 `vinfo`（控制台由 boot_args 填）取，**不需要任何新配置键**。

**【已验证】** ishtar 是小米 13 Ultra，**同 SM8550、同面板几何**（`0xb8000000`、1440x3200、
stride 5760），与本机一致。

**【已验证】** 依赖已满足：该脚本需要 `FrameBufferSerialPortLib/Font.h`（5x12 字形表），
`uefi-vermeer/Silicon/Silicium/SiliciumPkg/Library/FrameBufferSerialPortLib/Font.h` **已存在**。
它在 `uefi-sheng`/`ROOT.parent` 等路径里找，必要时用 `UNIXNU_UEFI_ROOT` 指定。

**【未验证】/ 必须注意**：ishtar 的 `configs/gpu_defconfig` 是 `CONFIG_XNU_BOOT_PROGRESS=y`
配 `NATIVE_UFS=n`/`NATIVE_VIRTIO_BLOCK=n`/`OWNER_ROOT_AUDIT=n`，但它**只覆盖了
`build-boot-progress.py`、没有覆盖 `build-xnu-4k-baseline.sh`**——按上游门禁这个组合应当被拒绝。
**作者未能证明 ishtar 那条路真的编得过。** 所以 (a)、(b) 缺一不可。

### 3.3 落地顺序（建议）

1. 复制上游 `scripts/build-xnu-4k-baseline.sh` 到本仓库 `scripts/`，按 (a) 改互锁并加 BOARD DEVIATION 注释
2. 取 ishtar 的 `package/BootProgressMin.c` 与 `scripts/build-boot-progress-min.py`，按本仓库路径调整
   （ishtar 里源文件在 `kits/sheng/BootProgressMin.c`，本仓库 kit 路径不同，需对应修改脚本里的 `SOURCE`）
3. 让设备侧 `scripts/build-boot-progress.py` 产出 `build/boot-progress/libBootProgress.a`
   —— **【已验证】** 这正是上游门禁之后那一步的产物路径，Min 脚本的 `OUT` 也是同一路径
4. `configs/vermeer_defconfig` 置 `CONFIG_XNU_BOOT_PROGRESS=y`
5. `[payload] kernel_args` 加 `boot-progress=2`（**【已验证】** sheng 的 `kernel_args` 里就是这个值）
6. `panic_screen` **保持 `true`**（理由见第 4.2 节）
7. `./unixnu kernel` → `./unixnu payload` → `./unixnu bootimg` → 刷机

遇到未定义符号失败时，把失败符号清单发回来——那正是 Min 脚本结尾那道检查要挡的东西。

---

## 第 4 节　刷下去怎么判读

### 4.1 屏幕现象对照表

| 现象 | 含义 |
| --- | --- |
| 和之前一样的静态画面、品红条还在 | 还死在同一处或更早 |
| **屏幕被清成黑** | 视频控制台建立成功 → **过墙了**（**【未验证】** 依据：kebab 记录称"内核创建控制台时会清屏"） |
| **出现文字日志面板**，检查点逐条刷出 | 第 1 步成功，**从此每面墙都会把原因印在屏幕上** |
| 黑屏 + 可读 panic 文字（含寄存器） | 过墙了，后面某处 panic，而且读得到了 |
| 色条都没有 | 回退一步：检查 `logs/payload-patch.log` 里探针是否装上，以及 `DARWINBOOT:` 行里的 framebuffer 基址与行跨度 |

### 4.2 为什么 `panic_screen` 要保持开启

**【已验证】** `panic_screen_main` 读 `_vinfo` 的 `v_baseaddr`：视频控制台建立**之前**那是
恒等物理地址（已失效，所以现在静默死）；建立**之后**它变成有效的内核 VA，panic 文字就能正常打出来。
**所以一旦第 1 步生效，`panic_screen` 从"累赘"变成有用的通道。**

**【已验证】** 上游 `docs/zh/guide/debugging.md`：第一条 panic 消息也会复制到
`boot_progress_panic_text`，实时横幅在屏幕重绘后仍保留文本。该缓冲属于 BootProgress 层，
所以它也是第 1 步的附带收益。

### 4.3 每次都要留的证据

**【已验证】** 上游 `docs/zh/guide/debugging.md#证据包` 要求：

| 项目 | 位置 |
| --- | --- |
| 诊断照片 | 标题栏、整页、trace 页、脉冲条 |
| panic 照片 | panic 屏幕整页 |
| 启动报告 | `out/<device>/<os_profile>/<config>/boot/boot.json` |
| 负载报告 | `out/<device>/<os_profile>/<config>/payload/payload.json` |
| 构建日志 | `out/<device>/<os_profile>/<config>/logs/` |

每张照片记录镜像哈希：

```sh
shasum -a 256 out/<device>/<os_profile>/<config>/boot/boot.img
jq . out/<device>/<os_profile>/<config>/boot/boot.json
```

**并且：每次只改一项。** 记录色条出现情况、是否自动复位、复位间隔秒数、屏幕上任何文字。

---

## 第 5 节　日志通道全部选项（按可达性排序）

| 通道 | 设备侧需要什么 | 主机侧 | 可达性 |
| --- | --- | --- | --- |
| **屏幕文字日志面板** | 第 3 节 (a)+(b)。字形表**已在** `uefi-vermeer` | 拍照 | **最近，无新驱动** |
| panic 屏幕文字 | 同上的附带收益 | 拍照 | 同上 |
| 固件二维码日志 | **【已验证】** `[payload] tools = true` + 固件 DSC/FDF 加入 `UniXNUPkg/Driver/AdvancedLogDxe/AdvancedLogDxe.inf`，并在 DSC `[LibraryClasses]` 映射 `AdvancedLoggerAccessLib`/`QrEncoderLib`，`[PcdsFixedAtBuild]` 设 `PcdUniXNUDeviceModel`。本仓库**都没有**（grep 全空） | 另一台手机扫码（OneSend） | 中等。**但它只能看到 `ExitBootServices` 之前**，拿不到内核阶段 |
| USB CDC 终端 | **【已验证】** 主机端工具**已在上游**：`scripts/debug_terminal.py`、`scripts/debug-terminal.cmd`（Windows CMD）、`scripts/terminal-requirements.txt`。但**设备侧驱动不在组件目录**（`components.toml` grep `UsbDebug` 为空），且上游明说"这些工具运行在电脑端，不会升级设备内核……修改 USB ID 本身不提供协议兼容性……板级驱动保留在设备仓库" | Windows 原生 | 远。需要移植设备侧 CDC 驱动 |
| 持久化 panic 区 | `/pram` 节点 + `/chosen` 的 `embedded-panic-log-size`。**【已验证】** 本仓库已有 `CONFIG_XNU_DT_PANIC_REGION=y`，但这两项都没提供，所以 `gPanicBase` 保持 0（不崩，也没日志） | 重启后读回 | 中等 |
| 持久化存储日志 | 需要阶段 6 存储先通。**【已验证】** 上游 `log-capture.md` 指向 sheng 的实现：`package/BootLog.c`、`BootReset.c`、`package/linux-log/`、`scripts/read-sheng-log.py` | Linux/recovery 读取器 | 阶段 6 之后 |

**【未验证】** 同 SoC 的 `/pram` 形状可参考：hanzi123-bilp/uefi-fuxi issue #4（小米 13，SM8550）称
`embedded-panic-log-size=0x80000`、`pram reg=<0x9fd80000 0x80000>`，并注明真机验证过。
**采用前务必用 `scripts/read-stock-layout.py` 确认该地址在本机内存图里是安全预留区。**

---

## 第 6 节　已排除清单（不要重复排查）

| 曾怀疑 | 结论 | 依据 |
| --- | --- | --- |
| CPU 服务只认 `qcom,kryo470`，不匹配 Cortex-A510 | **不存在** | **【已验证】** `drivers/ArmCpu/Info.plist`：`IOProviderClass=IOPlatformDevice`、`IOProbeScore=2000`、**无 compatible 过滤**；`ArmCpu.cpp:207-213` 校验的是 GIC 的 `arm,gic-v3` |
| 缺 timer 节点 / `virtual-timer-intid` | **不存在** | **【已验证】** `ArmDTPlatform.cpp` 全文 grep `timer\|intid\|timebase` 零命中；INTID 由 `ArmCpu.cpp` 常量 27 提供（`kSgiPpiEnable = 0xffffU \| (1U << 27)`）；ADT 侧 `devicetree.py:352` 只写每 CPU 的 `timebase-frequency` |
| GIC 编码不满足 `mapGic()` | **不存在** | **【已验证】** 逐项核过 `ArmCpu.cpp:196-230`：`/arm-io` `ranges` 需 24 字节（`devicetree.py:373-374` 正是 `<QQQ`）；`intc` `reg` 需 32 字节（两组 `<QQ`，`devicetree.py:252-253`）；`compatible` 需 11 字节；dist 尺寸 `0x10000`、redist `0x200000` 均满足下限；`redistributor-stride` 在 `devicetree.py:207` 的 `U64` 集合里按 `<Q` 编成 `0x40000`，在允许值内 |
| 缺必需 ADT 顶层节点 | **不存在** | **【已验证】** `/chosen`、`/cpus`、`/arm-io` 由 `devicetree.py:338/362/373` 生成；`/defaults`、`/options`、`/product` 由 `devicetree.py:376-377` **无条件**生成；NVRAM proxy 由 `empty_nvram_proxy()` 提供 |
| 缺 `/chosen` 必需属性 | **不存在** | **【已验证】** `osenvironment = "main"`、`crypto-hash-method = "sha2-384"`、`amfi-only-platform-code = 1` 来自 `profiles/os/ipados-17.7.11-21H461/profile.toml:60-69`，经 `devicetree.py:334` 的 `typed_properties` 进 `/chosen` |
| `memory-map` 占位符缺失导致 `EFI_COMPROMISED_DATA` | **不适用** | **【已验证】** `devicetree.py:335-337`：`PackedRAMDisk`/`TrustCache`/`GpuAddon` 按对应输入**条件生成**；没有该输入就不需要占位符 |
| 树莓派 panic-log 地址把我们打死 | **已规避** | **【已验证】** `configs/vermeer_defconfig` 已有 `CONFIG_XNU_DT_PANIC_REGION=y`，上游 `patches/xnu/native-dt-panic-region.patch` 跳过那次固定地址映射 |
| `payload.panic_framebuffer` 能指定帧缓冲 | **是死键** | **【已验证】** 上游 `scripts/` 全树 grep 零命中；ishtar 自己的 `scripts/` 也零命中，只出现在它的 `board.toml`/`porting.toml`/README。**不要抄** |
| `kt_fixed_zones` 没设 | **已设** | **【已验证】** `board.toml` 的 `kernel_args` 已含 `kt_fixed_zones=120` |
| AMP（E/P 簇）调度补丁缺失 | **已有** | **【已验证】** `patches/xnu/series` 含 `zzyf-sheng-amp.patch`；`dts/vermeer.dts` 给 cpu0-2 标 `"E"`、cpu3-7 标 `"P"`，与上游 `platform.md#clusters` 的 sheng 参考一致 |

---

## 第 7 节　仍是假设、尚未证明的那条

### 早期 I/O 页表预算

**【已验证】** XNU 的预算公式：

```c
va_l1_end = va_l1 + ((2 + (mem_segments * 10)) << 20);
va_l1_end += round_page(args->Video.v_height * args->Video.v_rowBytes);
va_l1_end = (va_l1_end + 0x7FFFFF) & ~0x7FFFFFULL;   /* 向上取整到 8 MiB */
```

**【已验证】** DarwinBoot 填的几何来自 `DarwinBootPkg/Driver/BootVideo.h`
（`Video[2]=RowBytes`、`Video[4]=VerticalResolution`）。代入本机：一份帧缓冲 =
3200 × 5760 = 18,432,000 B = **17.58 MiB**。单内存段时预算 = 12 MiB + 17.58 MiB = 29.58 MiB
→ 取整 **32 MiB**，视频控制台一份就吃掉 17.58 MiB。

**【已验证】** 上游**没有**这个补丁：`grep -rln "va_l1_end\|mem_segments\|pmap_map_bd" patches/` 为空。

**【未验证】但同机有先例**：ios-vermeer-lab 在**同一台 K70**（不同内核栈）第二次上机
"进了 XNU，到 kprintf startup complete，然后在 `pmap_map_bd` 里 panic，正在映射
generic video console"，修法是把帧缓冲那一项算**两份**。

**为什么仍是假设**：原计划用探针在 `_pmap_map_bd` 调 `_panic` 处复位来确认，但
`find_call()` 报 `_pmap_map_bd has 0 call(s) to _panic`（该版内核里 panic 被内联或改名），
**确认失败**。

**该补丁目前不在本仓库里**：**【已验证】** `patches/xnu/` 只有 `zzyd-sheng-audio-addon-mount.patch`、
`zzye-sheng-cpu-idle.patch`、`zzyf-sheng-amp.patch`，`patches/xnu/series` 也只列这三个。
负责本地构建的一方曾在自己的工作树里按真实源码重新生成过一份并通过了 `git apply`，但没有推到仓库。
如果要让它可复现，请把那份补丁和 `series` 里新增的一行一起提交。

不过**第 1 步做出来之后这个问题会自己回答**，所以它的优先级低于第 1 步。

---

## 第 8 节　后续阶段的已知待办（现在不做，但别忘）

| 项目 | 状态 | 依据 |
| --- | --- | --- |
| `[userdata]` 的 `first_lba`/`last_lba` 仍是 sheng 占位值（`last_lba = 0x3d0abfa` 超出实测本机 LUN0 末尾 `0x3afa3ff`） | 阶段 6 必须换成 `fastboot oem get-partition-info userdata` 的实测值 | **【已验证】** `board.toml` |
| `proprietary-files.txt` **整个是 sheng 的**：文件头自述 "Xiaomi Pad 6S Pro (sheng)"，`@fetch` 指向 `ianchb/sheng-firmware`，唯一条目是 Novatek NT36532/N81A/CSOT 触摸固件（平板的触控器） | 触摸阶段处理。**不要凭空改**：`scripts/extract-files.py` 按 SHA-256 校验并拒绝其他内容，正确条目必须从真机 dump 或可信镜像实测得出 | **【已验证】** 该文件全文 |
| SoC dtsi 的 GMU 基址是 sheng 的 `0x3d6a000`，原厂 DTB 是 **`qcom,gmu@3d69000`** | GPU 阶段。注意 SoC dtsi 是共享仓库，设备侧覆盖要在 `dts/vermeer.dts` 做 | **【已验证】** `scripts/read-stock-layout.py` 输出 |
| `dts/vermeer.dts` 把 `gpu`/`gmu`/`gpu_smmu` 设成 `okay`，但 SoC dtsi 里这三个**没有 `unixnu,adt-name`**，所以根本进不了 ADT | 现阶段无害（驱动匹配不上而已），**别去修** | **【已验证】** `devicetree.py:367` 只收带 `unixnu,adt-name` 的节点 |
| 显示后端需要预链接候选 `build/*-prelink/*candidate.macho` | 阶段 7。官方 `migration.md` 第 7 步列为**用户自备输入**，干净检出造不出来 | **【已验证】** `migration.md#build-inputs`、`configs/vermeer_defconfig` 顶部注释 |
| 本仓库缺 `porting.toml` | 上游 `docs/devices/vermeer-ahzcpp.md` 目前只是占位页（"Complete and validate porting.toml…"），补上才会发布真正的子系统记录 | **【已验证】** 上游该文件 |

---

## 第 9 节　一句话总结

**先用只读探针测出实际窗口和帧缓冲几何（零风险）；再把屏幕文字日志面板做出来
（放宽门禁 + 自包含观察者，两者配套）；之后每一面墙都会自己把原因印在屏幕上。
苹果图标在阶段 7，好几面墙之后，不要现在去追。**
