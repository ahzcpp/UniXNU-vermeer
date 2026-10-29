# vermeer (Redmi K70) 引导适配现状

本文给协作者（尤其是负责本地构建与刷机的一方）当参考。

**阅读约定**：下面严格区分三类内容。
- **【已验证】**：作者直接读过对应源码/配置文件确认，带 `文件:行号`。可以当事实用。
- **【未验证】**：来自别人的适配记录、README 或检索结果，**没有**在本仓库或上游源码里复核过。不要当事实用。
- **【已排除】**：曾经怀疑过、现在确认不是问题。列出来是为了**不要重复排查**。

许多 UniXNU 设备仓库自述为 AI 辅助，其文档会超前于代码。凡标【未验证】的，先自己复核再动手。

硬件实测值（原厂 UEFI 内存图、原厂 DTB、外设基址、GPU 观测）单独放在
[`docs/hardware-facts.md`](hardware-facts.md)，并附可重跑的 `scripts/read-stock-layout.py`。

---

## 1. 当前位置

UEFI → UniXNU 菜单 → DarwinBoot → XNU 入口都通了。屏幕上能看到：

| 色条 | 绘制者 | 含义 |
| --- | --- | --- |
| 绿 | DarwinBoot `DarwinEnter` | 已退出 boot services，准备跳内核 |
| 蓝 | 入口探针阶段 1 | 内核入口已执行，MMU 仍关闭 |
| 品红 | 入口探针阶段 2 | 到达开启 MMU 之前 |

`entry_probe_reset_after = "_arm_vm_init"` 的定时复位**会触发**，所以 `_arm_vm_init` 跑完并返回了。
之后屏幕静止、无文字、无后续复位。

---

## 2. 为什么我们"看不见"——这是本文最重要的一条

**【已验证】** `scripts/xnu-panic-screen.c:218-250`（上游）：`--panic-screen` 把 `_panic` 换成跳到这段代码，它这样取帧缓冲地址：

```c
volatile struct vc_info *v = (volatile struct vc_info *)VINFO;
g.base = (u8 *)v->v_baseaddr;        /* 裸地址 */
...
for (row = g.y; row < g.h; row++)     /* 先把整屏清成黑 */
  for (col = 0; col < g.w; col++)
    *(volatile u32 *)(g.base + row*g.rb + col*4) = 0;
```

在视频控制台建立之前，`v_baseaddr` 是 `0xb8000000` 这个**物理/恒等地址**。而 `arm_vm_init`
已经换掉了恒等映射，所以**清屏循环的第一个 store 就自己 data abort**，嵌套异常，屏幕上一个字都没有。

**结论：在这个阶段，任何 panic 都是静默的。我们看到的"卡死"可能本来就是 panic。**
在把可见通道做出来之前，逐指令二分测到的是这个级联，不是内核真实停点。

**【未验证】** 旁证：ace2 的 README（帧缓冲地址也是 `0xb8000000`）称 "the V=P map does not
cover 0xb8xxxxxx and the first store data-aborts"；kebab 的记录称"探针写死恒等映射，在
arm_vm_init 关掉该映射后会崩，看起来像内核卡死"。

---

## 3. 已排除的怀疑（不要重复排查）

| 怀疑 | 结论 | 依据 |
| --- | --- | --- |
| CPU 服务只认 `qcom,kryo470`，不匹配 Cortex-A510 | **不存在** | **【已验证】** `drivers/ArmCpu/Info.plist`：`IOProviderClass=IOPlatformDevice`、`IOProbeScore=2000`、**无 compatible 过滤**；`ArmCpu.cpp:207-213` 校验的是 GIC 的 `arm,gic-v3`。该结论来自 ios-sweet 那条链，不适用于本仓库 |
| 缺 timer 节点 / `virtual-timer-intid` | **不存在** | **【已验证】** `drivers/ArmDTPlatform/ArmDTPlatform.cpp` 全文 grep `timer\|intid\|timebase` 零命中；虚拟定时器 INTID 由 `ArmCpu.cpp` 常量 27 提供（`kSgiPpiEnable = 0xffffU \| (1U << 27)`）；ADT 侧 `devicetree.py:352` 只写每 CPU 的 `timebase-frequency` |
| GIC 编码不满足 `mapGic()` | **不存在** | **【已验证】** 逐项核过 `ArmCpu.cpp:196-230`：`/arm-io` 的 `ranges` 需 24 字节（`devicetree.py:373-374` 正是 `<QQQ`）；`intc` 的 `reg` 需 32 字节（两组 `<QQ`，`devicetree.py:252-253`）；`compatible` 需 11 字节（`"arm,gic-v3"`+NUL）；dist 尺寸 `0x10000` 不小于 `0x10000`、redist `0x200000` 不小于 `0x20000`；`redistributor-stride` 在 `devicetree.py:207` 的 `U64` 集合里按 `<Q` 编成 `0x40000`，落在允许值内。全部通过 |
| 树莓派 panic-log 地址（`PLATFORM_PANIC_LOG_PADDR`）把我们打死 | **已规避** | **【已验证】** `configs/vermeer_defconfig` 已有 `CONFIG_XNU_DT_PANIC_REGION=y`，上游 `patches/xnu/native-dt-panic-region.patch` 会跳过那次固定地址映射 |
| `payload.panic_framebuffer` 能给 panic 路径指定帧缓冲 | **是死键** | **【已验证】** 上游 `scripts/` 全树 grep 零命中；ishtar 自己的 `scripts/` 也零命中，只出现在它的 `board.toml`/`porting.toml`/README。**不要抄** |
| `kt_fixed_zones` 没设 | **已设** | **【已验证】** `board.toml` 的 `kernel_args` 已含 `kt_fixed_zones=120` |

---

## 4. 内存：我们和能跑通的 sheng 只差一个数字

**【已验证】** 逐字对过 `/home/.../ref-sheng`（`UniXNU/UniXNU-sheng`，作者自己那台小米平板 6S Pro，**同 SM8550**，上游记录 13/13 全实现）：

| | sheng | 本仓库 |
| --- | --- | --- |
| DTS `memory` | `<0 0x80000000 0 0x80000000>` | **一字不差** |
| DTS splash | `<0 0xb8000000 0 0x2b00000>` | **一字不差** |
| `payload.dram_size` | `0x80000000` | 一样 |
| `payload.adt.owned_memory_size` | **`0x130000000`**（4.75 GiB） | **`0x20000000`**（512 MiB） |

**【已验证】** `scripts/unixnu/devicetree.py:304-307`：ADT 交接只接受**一段连续** DRAM，且
`payload.dram_size` **必须等于** DTS `/memory` 的 size，不等就报错。
`devicetree.py:318-322`：`owned_memory_size` 是**独立**的另一个数，范围 256 MiB–8 GiB、2 MiB 对齐，
写进 ADT 的 `/chosen/darwinboot-memory-size`。

### 4.1 作者先前说错、现予更正

先前写过"512 MiB 根本不够跑 iPadOS"，**这条撤回**。依据是 asteroids 记录里
"384 MiB of owned memory left iPadOS short" 一句，检索未能找到其出处，属**【未验证】**。
反向的数据点（均**【未验证】**，但彼此独立）：

- uke 以 `0x10000000`（256 MiB）跑到 root-device 等待
- zeta 用 `0x18000000`（384 MiB）通过了同一个 BSD 检查点
- tb128fu 以 `0x80000000`（2 GiB）跑到 **iPadOS 17.7.11 主屏**

**所以 512 MiB 对早期启动阶段够用，它不是我们现在这面墙。** 窗口要加大是为了后面的用户态，
不是为了过 `arm_vm_init`。先别动它。

### 4.2 窗口为什么只有 512 MiB（供后续阶段参考）

**【未验证】** DarwinBoot 曾报告本机最大连续段约 `0x3B300000`（约 0.93 GiB），所以 sheng 的
4.75 GiB 请求必然 `NO WINDOW`。ace2 的记录说明机制：低位内存图留不出大段，需要在 UEFI 侧
把高位 DRAM 银行作为 `EfiConventionalMemory` 发布（它用 `ShengBootMemoryDxe`，
`0x840000000 + 0x140000000`）。UniXNU issue #55（nuwa，小米13 Pro，同 SM8550）称
"MemoryMapLib.c 的 DRAM 预留让 0xBEE00000 以上全部未映射"。

**【未验证】** K70 的高位银行 `0x8c0000000..0x940000000`（2 GiB）被 ios-vermeer-lab 以
32768 个稀疏采样照片确认过。

**这件事属于 `uefi-vermeer` 的工作，不是 board.toml 能解决的。排在可见通道之后。**

---

## 5. 建议方案

### 第 0 步：先测，不要猜（零风险，不用编译）

**【已验证】** `qusno1/vermeer-probe` 是专为 vermeer 写的**只读** UEFI 探针，GitHub Actions 构建，
`fastboot boot` 临时加载。它**不调 `ExitBootServices`、不写外设寄存器、不读写磁盘、不碰分区**，
只往帧缓冲输出。屏幕会打印：

- `WIN BASE` / `WIN LEN`：最大连续空闲窗口。**算法与 DarwinBoot 一致**（取最大连续
  `EfiConventionalMemory` 段，2 MiB 对齐后加 `0x4000`），所以这就是 `owned_memory_size` 的实测上限
- `RAM TOP` / `RAM MB`：最高连续常规内存顶端，`dram_size` 的参考
- `GOP FB` / `GOP RES` / `GOP STR` / `GOP FMT`：帧缓冲基址与几何（预期 `0xb8000000`、`1440x3200`）
- `EL` / `MIDR` / `MPIDR` / `AFF` / `CNTFRQ`（预期 `19200000`）

> 仓库 README 提醒：K70 是 `vermeer`，K70 Pro 是 `manet`，**不能混用**。

**把这几个数拍照记下来。** 之后所有关于窗口/DRAM 的讨论都以它为准，不再靠推测。

### 第 1 步：把失败变成可见（真正的效率解）

目标：让内核在 `arm_vm_init` 之后仍能往屏幕写字。两个零件都已找到：

**(a) 放宽 boot-progress 门禁**

**【已验证】** 上游 `scripts/build-xnu-4k-baseline.sh:78-87`：

```sh
if [ "$BOOT_PROGRESS" = 1 ]; then
  if [ "${XNU_NATIVE_UFS:-0}" != 1 ] || [ "${XNU_OWNER_ROOT_AUDIT:-0}" != 1 ] || \
     [ "${XNU_NATIVE_VIRTIO_BLOCK:-0}" != 1 ] || [ "${XNU_SOURCE_CORECRYPTO14:-0}" != 1 ]; then
    echo 'Boot progress requires the audited UFS/owner-root, Virtio and source-corecrypto build' >&2
    exit 1
```

这四个里 `OWNER_ROOT_AUDIT` 和 `NATIVE_VIRTIO_BLOCK` 需要 live-device GDB 观测产物
（见 `configs/vermeer_defconfig` 顶部注释），干净检出造不出来。

**【已验证】** `Yooglery/UniXNU-sirius` 的做法：把上游脚本整份复制到设备仓库 `scripts/`，
只改这一处互锁，注释写明是板级偏离：

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

机制可行：**【已验证】** `scripts/unixnu/tree.py` 的 `_copy_scripts` 按 base → soc → device 顺序复制，
**设备脚本覆盖同名上游脚本**。本仓库 `CONFIG_XNU_SOURCE_CORECRYPTO14=y` 已满足放宽后的条件。

**【未验证】** sirius 自称该面板在真机上跑出了实时内核日志。sirius 是 SDM710 的 eMMC 板，
不是 SM8550，其"UFS/owner-root/Virtio 集成点编译为空"的理由对我们是否同样成立**需要自己确认**。

**(b) 换一个自包含的观察者**

**不能**直接用本仓库现有的 `package/BootProgress.c`。

**【已验证】** `peter8889/UniXNU-ishtar` 的 `scripts/build-boot-progress-min.py` docstring 写明：
完整版"要读 UFS、Virtio block、ADSP、触摸、GPU 和 cpufreq 的计数器，精简配置会在约 118 个
未定义符号上链接失败"；`BootProgressMin.c` 用"内核镜像之外什么都不依赖"的方式实现了同样的十三个入口点。

**【已验证】** `ishtar/package/BootProgressMin.c`（730 行）的关键机制：

```c
/* arm_vm_init() installs the physical aperture; until then the variable is zero. */
extern const uint64_t physmap_base;
...
/* arm_vm_init() replaced the identity map, so the physical address is reachable only
   [through the aperture] */
uintptr_t aperture = (uintptr_t) physmap_base;
```

并且写屏之前用地址翻译指令先确认该地址已映射，**故障安全、不会自己崩**：

```c
__asm__ volatile ("at s1e1r, %1; isb; mrs %0, par_el1" : "=r" (par) : "r" (address) : "memory");
```

几何从 `vinfo`（控制台由 boot_args 填）取，**不需要任何新配置键**。
ishtar 是小米13 Ultra，**同 SM8550、同面板几何**（`0xb8000000`、1440x3200、stride 5760），
与本机完全一致。

**依赖已满足**：**【已验证】** 该脚本需要 `FrameBufferSerialPortLib/Font.h`（5x12 字形表），
`uefi-vermeer/Silicon/Silicium/SiliciumPkg/Library/FrameBufferSerialPortLib/Font.h` **已存在**。

**【未验证】/ 需要注意**：ishtar 的 `configs/gpu_defconfig` 是 `CONFIG_XNU_BOOT_PROGRESS=y`
配 `NATIVE_UFS=n`/`NATIVE_VIRTIO_BLOCK=n`/`OWNER_ROOT_AUDIT=n`，但它**只覆盖了
`build-boot-progress.py`、没有覆盖 `build-xnu-4k-baseline.sh`**——按上游门禁这个组合应当被拒绝。
作者没能证明 ishtar 那条路真的编得过。**所以 (a) 和 (b) 必须配套做**：用 sirius 的方式放宽门禁，
用 ishtar 的 Min 观察者替换完整版。

### 第 2 步：持久化 panic 日志（同 SoC 已有真机验证）

**【已验证】** 本仓库已有 `CONFIG_XNU_DT_PANIC_REGION=y`。上游
`patches/xnu/native-dt-panic-region.patch` 显示，开启后内核改走 DT 路径：读 `/chosen` 的
`embedded-panic-log-size` 和区域 `reg`，校验后才映射；**两者缺一就提前返回，`gPanicBase` 保持 0**
（即不崩，但也没有 panic 日志）。我们目前就没有这两项，所以这条路是空的。

**【未验证】** 同 SoC 的形状可参考：hanzi123-bilp/uefi-fuxi issue #4（小米13，SM8550）称
`embedded-panic-log-size=0x80000`、`pram reg=<0x9fd80000 0x80000>`，并注明"真机验证过"。
采用前请自行确认该地址在本机的内存图里是安全的预留区。

### 第 3 步（可选，和第 1 步独立）：早期 I/O 页表预算

**【未验证】但同机有先例**：ios-vermeer-lab 在**同一台 K70** 上，第二次上机"进了 XNU，
到 kprintf startup complete，然后在 `pmap_map_bd` 里 panic，正在映射 generic video console"，
修法是把早期 I/O 页表预算按**两份**帧缓冲计算。

**【已验证】** XNU 的预算公式（`osfmk/arm64/arm_vm_init.c`，见 lab 的
`xnu/vermeer/0019-vermeer-early-io-budget.patch` 上下文）：

```c
va_l1_end = va_l1 + ((2 + (mem_segments * 10)) << 20);
va_l1_end += round_page(args->Video.v_height * args->Video.v_rowBytes);
va_l1_end = (va_l1_end + 0x7FFFFF) & ~0x7FFFFFULL;   /* 向上取整到 8 MiB */
```

**【已验证】** DarwinBoot 填的几何：`DarwinBootPkg/Driver/BootVideo.h` 里
`Video[2]=RowBytes`、`Video[4]=VerticalResolution`。代入本机：一份帧缓冲 =
3200 × 5760 = 18,432,000 B = 17.58 MiB。单内存段时预算 = 12 MiB + 17.58 MiB = 29.58 MiB →
取整 32 MiB，视频控制台一份就吃掉 17.58 MiB。

**【已验证】** 上游**没有**这个补丁：`grep -rln "va_l1_end\|mem_segments\|pmap_map_bd" patches/` 为空。

**注意**：这一步**尚未被证明是我们的病根**。原计划用探针在 `_pmap_map_bd` 调 `_panic` 处复位来确认，
但 `find_call()` 报 `_pmap_map_bd has 0 call(s) to _panic`（该版内核里 panic 被内联或改名），**确认失败**。
所以它是个合理假设，不是结论。**第 1 步做出来之后，这个问题会自己回答。**

---

## 6. 省时间的几条机制（都已验证）

- **【已验证】** `scripts/unixnu/payload.py:127-143`：入口探针是在 **payload 阶段**装的——先
  `shutil.copyfile(kernel, payloads/'kernel.macho')`，再对副本跑 `patch-xnu-entry-probe.py`；ADT 也在
  payload 阶段生成。**所以改 `panic_screen`、任何 `entry_probe_*`、`owned_memory_size`、`dram_size`、DTS，
  都只需重跑 `payload` + `bootimg`，不用重编内核。** 一次内核构建可以出很多个 boot 镜像。
- **【已验证】** `patch-xnu-entry-probe.py`：阶段 1/2 会**画帧缓冲**（`PAINT`），阶段 3/4 只读
  `cntpct_el0` 然后 PSCI `SYSTEM_RESET`，**不碰帧缓冲**。`_panic`/`_vinfo` 的地址从**符号表**取
  （第 321-326 行），所以增删归档导致符号挪位会自动跟上。
- **【已验证】** `scripts/unixnu/patches.py` 的 `plan()`：`board.toml` 的
  `[kernel] patch_series` 已含 `device:patches/xnu/series`；该文件每行一个补丁名，按顺序 `git apply`，
  **任一失败就停**。改动 series 签名会让 `prepare()` 自动重新展开 XNU 源码树，不用手动清缓存。
- **【已验证】** `scripts/unixnu/devicetree.py:367`：**只有带 `unixnu,adt-name` 的节点才进 ADT**。
  本仓库 `dts/vermeer.dts` 把 `gpu`/`gmu`/`gpu_smmu` 设成 `okay`，但 SoC dtsi 里这三个
  **没有 `unixnu,adt-name`**，所以它们根本进不了 ADT。现阶段无害（驱动匹配不上而已），但别误以为已描述了 GPU。

---

## 7. 后续阶段的已知待办

- **【已验证】** `board.toml` 的 `[userdata]` `first_lba`/`last_lba` 仍是 sheng 的占位值
  （`last_lba = 0x3d0abfa` 超出实测的本机 LUN0 末尾 `0x3afa3ff`）。到存储阶段必须换成
  `fastboot oem get-partition-info userdata` 的实测值。
- **【未验证】** `yyx2025/UniXNU-vermeer` 的 README 记录了 K70 与 sheng 的真实差异：触摸是
  Goodix `gt9916S`（SE4，GPIO 44–47，IRQ 25，reset 24），**不是** sheng 的 Novatek；
  K70 的 GMU 窗口是 `0x3d68000`，而 SoC dtsi 里是 sheng 的 `0x3d6a000`。到 GPU/触摸阶段需自行复核。
- **【已验证】** 上游 `docs/devices/vermeer-ahzcpp.md` 目前只是占位页，写着
  "Complete and validate porting.toml in the device repository to publish its subsystem record"。
  本仓库需要补一个 `porting.toml` 才会发布真正的子系统记录。

---

## 8. 执行顺序

1. **第 0 步**（`vermeer-probe`，只读、`fastboot boot`）→ 拿到 `WIN BASE`/`WIN LEN`/`RAM TOP`/`GOP *` 实测值
2. **第 1 步 (a)+(b) 配套**（放宽门禁 + Min 观察者）→ 让 `arm_vm_init` 之后的失败可见
3. 看屏幕说了什么，再决定第 2/3 步

判读提示：**【未验证】** kebab 的记录称"内核创建控制台时会清屏"，所以**屏幕被清成黑**可能本身就是
视频控制台建立成功的信号；和"仍是静态品红"要区分开。

每一步只改一项，记录色条出现情况、是否自动复位、复位间隔秒数，以及屏幕上的任何文字。

---

## 9. 苹果图标和进度条是谁画的

这是本项目最常被误解的一点，先把它说清楚，免得往错的方向使劲。

**【已验证】** 本仓库的 `package/BootProgressDisplay.h` 只有 61 行，它画的是**等宽字形的文字行**：

```c
unsigned bound, width, height, stride, scale;
...
if (!d->bound || row >= 48 || (row+1)*14*d->scale >= d->height) return;
unsigned cols = d->width / (6*d->scale);
```

列数 = `宽度 / (6×scale)`，行高 `14×scale`，最多 48 行。**它是一个屏幕文字日志面板，
不画进度条，也不画苹果图标。**

**【已验证】** 官方 `docs/zh/porting/bring-up.md` 把阶段分得很清楚：

- **阶段 5 内核入口**：目标是"XNU 启动并到达平台专家"
- **阶段 6 存储与根卷**：目标是"块驱动发布 userdata 分区，卷出现，XNU 挂载根卷"、
  "内核挂载根卷并启动 `launchd`"
- **阶段 7 用户态**：目标是"`launchd` 运行，首个用户进程到达可见状态"，检查项包含
  "**显示服务或控制台有输出**"

**所以苹果图标属于阶段 7，由用户态的显示服务画，不是内核引导期的任何东西画的。**

**【未验证】** 旁证（来自 ios-vermeer-lab 的记录，不同的内核栈，但同一台 K70）：它的苹果图标
出现在 "first presentation: backboardd alternates two 1125x2436 BGRA surfaces on the guarded
1440x3200 panel" 的时候——也就是 `backboardd` 开始提交显示表面之后。

### 9.1 这意味着什么

要看到苹果图标，必须依次走完：

1. 阶段 5 内核跑过平台专家（**我们现在卡在这里**）
2. 阶段 6 存储：UFS 驱动发布 userdata、根卷 UUID 匹配、挂上根卷、起 `launchd`
3. 阶段 7 用户态：显示服务发布并提交第一帧

**【已验证】** 而阶段 7 的显示后端（官方 `docs/zh/porting/display.md`）需要 `GopFramebuffer`
组件加 `CONFIG_XNU_NATIVE_LEGACY_DISPLAY=y`，并由 `scripts/build-gop-legacy-display.py`
"使用预链接候选内核验证 ABI 方法"再生成后期显示归档。而那个预链接候选
（`build/*-prelink/*candidate.macho`）在官方 `migration.md` 第 7 步被列为**用户自备的构建输入**，
干净检出造不出来——这也是本仓库 `configs/vermeer_defconfig` 当初关掉
`CONFIG_XNU_NATIVE_LEGACY_DISPLAY` 的原因（见该文件顶部注释）。

**结论：苹果图标不是下一步，它在好几面墙之后。不要把它当作近期目标去调。**

### 9.2 近期能看到的"可见进展"是什么

按真实可达性排序：

| 里程碑 | 需要什么 | 距离 |
| --- | --- | --- |
| 屏幕出现**文字日志面板**（检查点逐条刷出来） | 第 5 节第 1 步的 (a)+(b) | **下一步** |
| 屏幕被**清成黑**（视频控制台建立） | 同上，或当前这面墙被解开 | 下一步的副产品 |
| 可读的 **panic 文字**（含寄存器） | 同上 | 下一步的副产品 |
| 根卷挂上、`launchd` 起来 | 阶段 6，需要实测 GPT 与 UUID | 之后 |
| **苹果图标 + 进度条** | 阶段 7，需要预链接候选等自备输入 | 更后面 |

**把"文字日志面板"当成下一个目标。** 它一旦出来，后面每一面墙都会直接把原因打在屏幕上，
不用再靠重启计时反推——这才是真正把速度提上来的那一步。
