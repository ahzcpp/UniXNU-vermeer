# 从零到刷机：完整操作手册

**读者**：负责本地构建与刷机的人。本页假设你**没有**任何背景知识，从环境准备讲到刷机判读。

配套文档（建议按顺序读）：

| 文档 | 作用 |
| --- | --- |
| 本页 | **怎么做**：环境、命令、刷机、本轮两步的完整操作 |
| [`docs/playbook.md`](playbook.md) | **做什么**：阶段位置、判读对照表、日志通道排序、已排除清单 |
| [`docs/bring-up.md`](bring-up.md) | **为什么**：推理与依据 |
| [`docs/hardware-facts.md`](hardware-facts.md) | **实测值**：原厂内存图、外设基址，带出处 |
| [`docs/lab-crossref.md`](lab-crossref.md) | **怎么用 lab**：`ios-vermeer-lab` 不是 UniXNU，哪些能用哪些不能 |

**标注约定**（全部文档一致）：
**【已验证】** 作者直接读过对应源码/配置确认 ·
**【未验证】** 来自他人记录，未复核 ·
**【已排除】** 确认不是问题，不要重复排查

---

## 第 1 部分　先想明白：这套东西是怎么运作的

在敲命令之前，先把链路在脑子里过一遍。不理解链路，报错就只能瞎猜。

### 1.1 开机之后发生了什么

```
手机上电
  → 高通 XBL/ABL（原厂引导，我们不碰）
      → 它加载我们刷进 boot 分区的 boot.img
          → boot.img 里是 UEFI 固件（Mu-Silicium，来自 uefi-vermeer）
              → UEFI 启动 UniXNU（菜单程序）
                  → 菜单读 payload 卷里的 EFI/UniXNU/unixnu.toml
                      → 选中 xnu 启动项，交给 DarwinBoot
                          → DarwinBoot 校验内核/ADT，申请内存窗口，
                            退出 boot services，画绿条，跳进 XNU
                              → XNU 跑起来（我们现在卡在这一段）
```

**对应的官方阶段编号**（上游 `docs/zh/porting/bring-up.md`）：
1 UEFI → 2 UniXNU → 3 负载 → 4 DarwinBoot → 5 内核入口 → 6 存储与根卷 → 7 用户态。

**我们在阶段 5。** 阶段 1-4 全部通过。

### 1.2 boot.img 里到底打包了什么

`./unixnu bootimg` 把三样东西合成一个 Android boot image：

| 成分 | 来自哪 | 谁生成 |
| --- | --- | --- |
| **固件卷（FD）** | `uefi-vermeer` 编译产物，里面嵌了 UniXNU、DarwinBoot、UniXNURamDiskDxe、DiskIo、FAT | `./unixnu firmware` |
| **固件 DTB** | 原厂 DTB（给 ABL 看的，不是给 XNU 看的） | 你本地提供的文件 |
| **ramdisk = payload 卷** | 一个 FAT 镜像，里面是 `EFI/UniXNU/unixnu.toml` + 内核 + ADT + 可选 sidecar | `./unixnu payload` |

**关键区分**（这点经常搞混）：
- **固件 DTB** 是给高通 ABL 的，`bootimg --dtb` 传入
- **ADT**（Apple Device Tree）才是给 XNU 的，由 `./unixnu payload` 阶段从
  `dts/vermeer.dts` + SoC dtsi 编译并生成，和固件 DTB 是两回事

**【已验证】** 出处：`scripts/unixnu/payload.py`（payload 阶段调 `devicetree.compile_hardware` +
`generate`）、`scripts/unixnu/cli.py:59-63`（bootimg 的 `--fd` / `--dtb` / `--ramdisk`）。

### 1.3 哪些改动要重编内核，哪些不用——**这是最省时间的一条**

**【已验证】** `scripts/unixnu/payload.py:127-143`：入口探针是在 **payload 阶段**装的——
先把内核**复制**一份到 `payloads/kernel.macho`，**再**对那个副本打补丁；ADT 也在 payload 阶段生成。

| 你改了什么 | 要重跑什么 | 大概耗时 |
| --- | --- | --- |
| `[payload] panic_screen`、任何 `entry_probe_*` | `payload` → `bootimg` | 分钟级 |
| `[payload] kernel_args`、`owned_memory_size`、`dram_size` | `payload` → `bootimg` | 分钟级 |
| `dts/vermeer.dts` | `payload` → `bootimg` | 分钟级 |
| `configs/*_defconfig`、`patches/xnu/*`、`[kernel] archives` | `kernel` → `payload` → `bootimg` | **30–60 分钟** |
| `uefi-vermeer` 源码、固件 DSC/FDF | `firmware` → `payload` → `bootimg` | 看固件 |
| `package/*.c`（板级 package） | `package` → `payload` → `bootimg` | 中等 |

**一次内核构建可以出很多个 boot 镜像。** 一轮里把多个 payload 变体都生成好、改名存着，连着刷，
**不要一次改一个参数就重编内核**。

---

## 第 2 部分　环境准备（只做一次）

### 2.1 目录摆放

三个仓库放成**同级**：

```
工作目录/
├── UniXNU/              ← 主仓库，所有命令在这里面跑
├── UniXNU-vermeer/      ← 本仓库（设备仓库）
└── uefi-vermeer/        ← 固件仓库
```

```sh
git clone --recurse-submodules https://github.com/UniXNU/UniXNU
git clone --recurse-submodules https://github.com/ahzcpp/UniXNU-vermeer
git clone --recurse-submodules https://github.com/yyx2025/uefi-vermeer -b vermeer
```

**【已验证】** `cli.py` 的 `need_uefi()` 会检查 `<uefi>/Resources/Scripts/mkbootimg.py` 是否存在；
不存在就报 "is not a Mu-Silicium checkout"。

### 2.2 Linux 工具链（编 XNU 必需）

**【已验证】** `scripts/linux/setup-toolchain.py` 的用法说明：

```sh
sudo scripts/linux/setup-toolchain.py            # 默认装到 /opt/unixnu-linux
# 或者指定前缀：
sudo scripts/linux/setup-toolchain.py /path/to/prefix
```

它会构建带 `__builtin_xnu_type_*` 扩展的 clang、cctools-port（ld64/libtool/nm/lipo）、
mig、iig-port、lzfse，并摆成 xnu 的 makefile 期望的目录结构。
前缀也可以用环境变量 `UNIXNU_TOOLCHAIN` 指定。

**这一步耗时很长（编译 LLVM），但只做一次。**

### 2.3 本地构建输入 `unixnu.local.toml`

放在 **UniXNU 主仓库根目录**。从示例复制：

```sh
cp unixnu.example.toml unixnu.local.toml
```

**【已验证】** `scripts/unixnu/config.py:32-53`，`[build]` 表只允许这些键，写错会直接报
`unknown build option`：

```
device  out  uefi  edk2  kernelcache  kernel  trust_cache  owner_trust_cache
data_ramdisk  gpu_addon  dtb  fd  ramdisk  output
ipsw  unique_chip_id  jobs  target  kernel_args
```

本机最小可用的样子：

```toml
[build]
device = "../UniXNU-vermeer"
uefi = "../uefi-vermeer"
dtb = "/绝对路径/vermeer-firmware.dtb"
kernelcache = "/绝对路径/kernelcache.release.iphone10b"   # 从 IPSW 取出的解压 kernelcache
unique_chip_id = "0123456789ABCDEF"
jobs = 4
```

**`unique_chip_id` 是必填的。** **【已验证】** `board.toml:208` 是 `unique_chip_id = "required"`，
`devicetree.py:311-315` 要求一个**非零 u64**（十六进制）。它和 `chip_id` 一起生成 `IOPlatformUUID`，
**不需要从手机读**，任意非零值即可，但选定后别再改（改了 UUID 会变）。

**【已验证】** 对应的环境变量（优先级低于命令行、高于本地配置文件）：
`UNIXNU_DEVICE`、`UNIXNU_OUT`、`UNIXNU_UEFI`、`UNIXNU_EDK2`、`UNIXNU_KERNELCACHE`、
`UNIXNU_DTB`、`UNIXNU_IPSW`、`UNIXNU_UNIQUE_CHIP_ID`、`JOBS`。

### 2.4 取 IPSW 组件

**【已验证】** `board.toml:18-27` 已经写好了目标 IPSW：

```toml
[ipsw]
url = "https://updates.cdn-apple.com/.../iPad_Pro_HFR_17.7.11_21H461_Restore.ipsw"
product = "iPad7,1"
version = "17.7.11"
build = "21H461"
device_class = "j120ap"
chip_id = 0x8011
board_id = 12
fetch = ["KernelCache", "DeviceTree", "StaticTrustCache", "Cryptex1,SystemTrustCache"]
```

```sh
./unixnu ipsw info          # 看会取哪些组件
./unixnu ipsw fetch         # 取出来（小文件）
```

取出的 KernelCache 需要解压后填到 `kernelcache = `。

---

## 第 3 部分　构建命令逐条

所有命令**在 UniXNU 主仓库目录里**执行。填好 `unixnu.local.toml` 后不必每次加 `-D`。

### 3.1 一次完整构建的顺序

**上一条成功了，再跑下一条。**

```sh
cd UniXNU

./unixnu defconfig vermeer_defconfig    # 选内核配置（本仓库 configs/ 下）
./unixnu fetch                          # 拉取并钉住 SoC/组件源码，写 unixnu.lock
./unixnu status --json                  # 核对解析结果（见 3.3）
./unixnu tree                           # 组装构建树
./unixnu kernel                         # 编 XNU + 预链接       ← 慢（30–60 分钟）
./unixnu build package                  # 编板级 package
./unixnu firmware                       # 编 UEFI 固件（需要 --uefi）
./unixnu payload                        # 装探针 + 生成 ADT + 打 payload 卷
./unixnu bootimg                        # 合成 boot.img
```

**【已验证】** 可用子命令（`cli.py:48-69`）：
`defconfig` `fetch` `tree` `build` `kernel` `package` `firmware` `payload` `userdata`
`prelink` `bootimg` `ipsw` `status` `clean`。
`kernel`/`package`/`firmware`/`payload`/`userdata`/`prelink` 都是 `build <step>` 的快捷别名。

> `userdata` 是可选步骤，产物很大，**只在显式点名时才构建**，当前阶段不需要。

### 3.2 常用选项

**【已验证】** 来自 `cli.py:20-42`：

| 选项 | 作用 |
| --- | --- |
| `-D DIR` / `--device DIR` | 设备仓库路径（会被记住） |
| `--uefi DIR` | Mu-Silicium 检出目录 |
| `--kernelcache FILE` | 原始解压 kernelcache |
| `--unique-chip-id HEX` | 平台种子 |
| `-j N` / `--jobs N` | 并行度 |
| `--kernel-arg NAME[=VALUE]` | 追加一个启动参数，**可重复** |
| `--skip-preflight` | 跳过固件步骤的板级预检查（`scripts/check-vermeer.py`） |
| `--out DIR` | 输出根目录 |
| `--config FILE` | 用别的本地配置文件 |

`bootimg` 独有：`--fd FILE`、`--dtb FILE`、`--ramdisk FILE|none`、`-o FILE`。

### 3.3 每步该检查什么

**【已验证】** 上游 `docs/zh/porting/migration.md` 第 8 步给的对照表：

| 相对解析输出目录的路径 | 检查内容 |
| --- | --- |
| `.config` | 必需设备开关和 archive 路径 |
| `tree/.cache/xnu-work/` | 打完补丁的 XNU 源码（**要手工改 XNU 就在这里找文件**，但别直接改，见 5.3） |
| `kernel/kernel.macho` | 内核链接与预链接成功 |
| `device-tree/hardware.dtb` | payload 阶段编译的硬件树 |
| `payload/payload.img` | 内核、ADT 和可选输入 |
| `payload/payload.json` | **最终启动参数**和 payload 输入 |
| `boot/boot.img` | 打包成功 |
| `logs/` | 各步骤日志 |

输出目录形如：

```
out/vermeer/ipados-17.7.11-21H461/vermeer/
```

取路径的办法：

```sh
./unixnu status --json | python3 -c 'import json,sys; print(json.load(sys.stdin)["output"])'
```

### 3.4 核对启动参数真的生效了

**【已验证】** 上游 `docs/zh/xnu/boot-arguments.md#apply-arguments`：

```sh
OUT=$(./unixnu status --json | python3 -c 'import json,sys; print(json.load(sys.stdin)["output"])')
jq -r .kernel_args "$OUT/payload/payload.json"
mtype -i "$OUT/payload/payload.img" ::/EFI/UniXNU/unixnu.toml
```

生效链路：`board.toml` 的 `[payload] kernel_args` → `unixnu.local.toml` 的
`[build] kernel_args` → 命令行 `--kernel-arg`，**后面的覆盖同名的**。

判读规则：
- payload 命令输出的 `kernel args:` 与 DarwinBoot 的 `DARWINBOOT: command line:` **不一致**
  → 检查选中的卷和启动项
- 两者**一致但行为没变** → 内核或驱动没实现这个参数

### 3.5 探针装没装上

```sh
cat "$OUT/logs/payload-patch.log"
```

**【已验证】** `payload.py:142` 把 `patch-xnu-entry-probe.py` 的输出写到这个文件。
装不上会在这里报原因（例如 `_pmap_map_bd has 0 call(s) to _panic`）。

---

## 第 4 部分　刷机

### 4.1 两种完全不同的刷法，别混用

| 场景 | 命令 | 性质 |
| --- | --- | --- |
| **只读探针**（第 5 部分第 0 步） | `fastboot boot <probe.img>` | **临时加载，不写任何分区**。断电即恢复 |
| **我们自己的 boot.img** | `fastboot flash boot_b <boot.img>` | **写入 boot_b 分区** |

本机系统已损坏，所以我们的镜像走 `flash boot_b`。**但只读探针必须用 `fastboot boot`**——
它的全部价值就在于不改变设备状态。

### 4.2 刷我们的镜像

```sh
fastboot devices                                     # 确认连上
fastboot getvar current-slot                         # 看当前槽位
fastboot flash boot_b "$OUT/boot/boot.img"
fastboot set_active b                                 # 需要时切到 b 槽
fastboot reboot
```

### 4.3 回到 fastboot

屏幕卡住、或探针定时复位之后：长按电源+音量下，进 fastboot，`fastboot devices` 确认。

### 4.4 每次刷机都要留的证据

**【已验证】** 上游 `docs/zh/guide/debugging.md#证据包`：

```sh
shasum -a 256 "$OUT/boot/boot.img"
jq . "$OUT/boot/boot.json"
jq . "$OUT/payload/payload.json"
```

加上：**整屏照片**（色条、文字、panic 页）、**是否自动复位**、**复位间隔秒数**、
以及 `$OUT/logs/` 整个目录。

**每次只改一项**，否则现象无法归因。

---

## 第 5 部分　本轮要做的两步（完整操作）

背景与依据见 `docs/playbook.md`。这里只给操作。

### 第 0 步　只读探针测实际窗口（零风险，不用编译）

#### 为什么必须先做

**【已验证】** 跑 `python3 scripts/read-stock-layout.py`（在**设备仓库**里跑）可自行复核：
本仓库三份静态来源**一致地**看不到高位 DRAM 银行——原厂 UEFI 图最大
`EfiConventionalMemory` 段只有 **209.8 MiB**；原厂 DTB 的 `/memory` 是 **`0 + 0` 占位符**
（高通惯例，XBL/ABL 运行时从 SMEM 填）；原厂 reserved-memory 全部低于 4 GiB。

**所以"这台机器能给多大窗口"在本仓库里无解，只能在设备上测。**

#### 怎么做

**【已验证】** `qusno1/vermeer-probe` 是专为 vermeer 写的只读 UEFI 探针：
**不调用 `ExitBootServices`、不写外设寄存器、不读写磁盘、不碰任何分区**，
GitHub Actions 构建、`fastboot boot` 临时加载。

> 该仓库 README 提醒：K70 是 `vermeer`，K70 **Pro** 是 `manet`，**不能混用**。

```sh
fastboot devices
fastboot boot probe-boot.img      # 注意：boot，不是 flash
```

#### 要拍照记下的字段

| 字段 | 用途 | 预期 |
| --- | --- | --- |
| `WIN BASE` / `WIN LEN` | **最大连续空闲窗口**，算法与 DarwinBoot 一致（最大连续 `EfiConventionalMemory` 段，2 MiB 对齐 +`0x4000`）→ 这就是 `owned_memory_size` 的实测上限 | 未知，这就是要测的 |
| `RAM TOP` / `RAM MB` | 最高连续常规内存顶端 → `dram_size` 参考 | 未知 |
| `GOP FB` / `GOP SZ` | 帧缓冲基址/大小 | `0xb8000000` |
| `GOP RES` | 分辨率 | `1440x3200` |
| `GOP STR` | 每行**像素**数 | `1440` |
| `GOP FMT` | 像素格式 | 记录即可 |
| `EL` | 异常级别 | EL1 |
| `CNTFRQ` | 定时器频率 | **必须 `19200000`** |
| `MIDR` / `MPIDR` / `AFF` | CPU 标识与启动核 affinity | 记录 |
| `MMFR0` / `PFR0` | 特性寄存器 | 记录 |

#### 拿到数之后怎么判断

- `CNTFRQ` ≠ `19200000` → SoC dtsi 的 `timebase-frequency` 和实机不符，**停下来先报告**
- `GOP RES`/`GOP STR` 与 `1440x3200`/`1440` 不符 → 帧缓冲几何假设要改，**影响所有屏幕输出**
- `WIN LEN` < `0x20000000` → 当前窗口请求过大，DarwinBoot 会 `NO WINDOW`
- `WIN LEN` 远大于 `0x20000000` → 记下来，**但现在不要改窗口**（理由见下）

#### 为什么现在不改窗口

**【已验证，并更正先前说法】** 先前说过"512 MiB 根本不够跑 iPadOS"，**此条撤回**：
其依据（asteroids 记录的 "384 MiB left iPadOS short"）检索未能找到出处，属**【未验证】**。
反向数据点三个、彼此独立（均**【未验证】**）：uke 以 256 MiB 跑到 root-device 等待；
zeta 以 `0x18000000`（384 MiB）通过同一 BSD 检查点；tb128fu 以 `0x80000000`（2 GiB）
跑到 iPadOS 17.7.11 主屏。

**512 MiB 对阶段 5 够用。窗口是阶段 6/7 的事。现在动它只会同时引入新变量。**

### 第 1 步　把失败变成可见（本阶段真正的解）

#### 现在为什么什么都看不到

**【已验证】** 上游 `scripts/xnu-panic-screen.c:218-250`（`--panic-screen` 把 `_panic`
换成跳到这段）：

```c
volatile struct vc_info *v = (volatile struct vc_info *)VINFO;
g.base = (u8 *)v->v_baseaddr;        /* 裸地址 */
...
for (row = g.y; row < g.h; row++)     /* 先把整屏清成黑 */
  for (col = 0; col < g.w; col++)
    *(volatile u32 *)(g.base + row*g.rb + col*4) = 0;
```

视频控制台建立之前 `v_baseaddr` 是 `0xb8000000` 这个**物理/恒等地址**；
`arm_vm_init` 已经换掉恒等映射，**清屏循环的第一个 store 就自己 data abort**，
嵌套异常 → 屏幕上一个字都没有。

**所以这个阶段任何 panic 都是静默的。我们看到的"卡死"可能本来就是 panic。**

#### 两个零件必须配套做（缺一不可）

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

其中 `OWNER_ROOT_AUDIT` 和 `NATIVE_VIRTIO_BLOCK` 需要 live-device GDB 观测产物
（见 `configs/vermeer_defconfig` 顶部注释），干净检出造不出来。

**操作**（照 `Yooglery/UniXNU-sirius` 的做法，**【已验证】** 读过它的脚本）：

```sh
# 在设备仓库里
cp ../UniXNU/scripts/build-xnu-4k-baseline.sh scripts/
```

然后把上面那段互锁改成只要求 source crypto，并写明板级偏离：

```sh
if [ "$BOOT_PROGRESS" = 1 ]; then
  # BOARD DEVIATION from the UniXNU script (keep in sync when re-basing):
  # OWNER_ROOT_AUDIT and NATIVE_VIRTIO_BLOCK need a live-device observation this
  # checkout cannot produce. Only the source crypto provider is required here.
  if [ "${XNU_SOURCE_CORECRYPTO14:-0}" != 1 ]; then
    echo 'Boot progress requires the source-corecrypto build' >&2
    exit 1
  fi
  python3 scripts/build-boot-progress.py
fi
```

**为什么这样就能生效**：**【已验证】** `scripts/unixnu/tree.py` 的 `_copy_scripts` 按
base → soc → device 顺序复制，**设备脚本覆盖同名上游脚本**。
本仓库 `CONFIG_XNU_SOURCE_CORECRYPTO14=y` 已满足放宽后的条件。

> **维护成本**：复制上游脚本意味着上游更新时要重新基线化。注释里必须留
> "keep in sync when re-basing"，和 sirius 一样。

**(b) 换一个自包含的观察者**

**不能**直接用本仓库现有的 `package/BootProgress.c`。

**【已验证】** `peter8889/UniXNU-ishtar` 的 `scripts/build-boot-progress-min.py` docstring：
完整版"要读 UFS、Virtio block、ADSP、触摸、GPU 和 cpufreq 的计数器，
**精简配置会在约 118 个未定义符号上链接失败**"；
`BootProgressMin.c` 用"**内核镜像之外什么都不依赖**"的方式实现同样的十三个入口点。

**【已验证】** 它的关键机制（730 行）：

```c
/* arm_vm_init() installs the physical aperture; until then the variable is zero. */
extern const uint64_t physmap_base;
...
uintptr_t aperture = (uintptr_t) physmap_base;
```

写屏前用地址翻译指令确认该地址已映射，**故障安全、不会自己崩**：

```c
__asm__ volatile ("at s1e1r, %1; isb; mrs %0, par_el1" : "=r" (par) : "r" (address) : "memory");
```

几何从 `vinfo`（控制台由 boot_args 填）取，**不需要任何新配置键**。
ishtar 是小米 13 Ultra，**同 SM8550、同面板几何**（`0xb8000000`、1440x3200、stride 5760）。

**依赖已满足**：**【已验证】** 它需要 `FrameBufferSerialPortLib/Font.h`（5x12 字形表），
`uefi-vermeer/Silicon/Silicium/SiliciumPkg/Library/FrameBufferSerialPortLib/Font.h` **已存在**。
脚本在 `uefi-sheng`/上级目录等处找，必要时用 `UNIXNU_UEFI_ROOT` 指定。

**操作步骤**：

1. 取 ishtar 的 `package/BootProgressMin.c` 与 `scripts/build-boot-progress-min.py`
2. 调整脚本里的 `SOURCE` 路径（ishtar 里源文件在 `kits/sheng/BootProgressMin.c`，
   本仓库 kit 路径不同）
3. 让设备侧 `scripts/build-boot-progress.py` 产出
   `build/boot-progress/libBootProgress.a` ——**【已验证】** 这正是上游门禁之后那一步的
   产物路径，Min 脚本的 `OUT` 也是同一路径
4. `configs/vermeer_defconfig` 置 `CONFIG_XNU_BOOT_PROGRESS=y`
5. `board.toml` 的 `[payload] kernel_args` 加 `boot-progress=2`
   （**【已验证】** sheng 的 `kernel_args` 里就是这个值）
6. `[payload] panic_screen` **保持 `true`**（理由见 6.2）

**【未验证】/ 注意**：ishtar 的 `gpu_defconfig` 是 `BOOT_PROGRESS=y` 配
`NATIVE_UFS=n`/`NATIVE_VIRTIO_BLOCK=n`/`OWNER_ROOT_AUDIT=n`，但它**只覆盖了
`build-boot-progress.py`、没覆盖 `build-xnu-4k-baseline.sh`**——按上游门禁这个组合应当被拒绝。
**所以 (a) 必须和 (b) 一起做**，单做一半会在约 118 个未定义符号上链接失败。

#### 构建并刷机

```sh
cd UniXNU
./unixnu kernel && ./unixnu payload && ./unixnu bootimg
OUT=$(./unixnu status --json | python3 -c 'import json,sys; print(json.load(sys.stdin)["output"])')
shasum -a 256 "$OUT/boot/boot.img"
fastboot flash boot_b "$OUT/boot/boot.img" && fastboot reboot
```

链接失败时，把未定义符号清单发回来——那正是 Min 脚本结尾那道检查要挡的东西。

### 5.3 要手工改 XNU 源码时的正确做法

**不要**直接改 `tree/.cache/xnu-work/`——**【已验证】** `scripts/unixnu/patches.py` 的
`prepare()` 在补丁签名变化时会**删掉并重新展开**整棵源码树，你的改动会丢。

正确流程：

1. 在 `tree/.cache/xnu-work/` 里找到并改好文件（用来确认改法）
2. 在那棵树里 `git diff > /tmp/x.patch`
3. 把补丁放进**设备仓库** `patches/xnu/`，文件名加进 `patches/xnu/series`
4. 重跑 `./unixnu kernel`

**【已验证】** `patches.py` 的 `plan()`：`board.toml` 的 `[kernel] patch_series` 已含
`device:patches/xnu/series`；该文件每行一个补丁名，按顺序 `git apply`，**任一失败就停**。
改动 series 会让 `prepare()` 自动重新展开源码树，不用手动清缓存。

> **从 `ios-vermeer-lab` 拿来的补丁必须在我们自己的源码树里重新生成**，不能直接用它的文件：
> 两边 XNU 不同代、行号不同，而且它的 hunk 常带 `#if defined(SWEET)` 守卫。
> 详见 `docs/lab-crossref.md` 第 3 节（那里有一个真实的失败案例）。

---

## 第 6 部分　刷下去怎么判读

### 6.1 屏幕现象对照表

| 现象 | 含义 | 下一步 |
| --- | --- | --- |
| 和之前一样的静态画面、品红条还在 | 还死在同一处或更早 | 检查 `logs/` 和构建是否真的带上了改动 |
| **屏幕被清成黑** | 视频控制台建立成功 → **过墙了**（**【未验证】** 依据：kebab 记录称"内核创建控制台时会清屏"） | 继续看有没有文字 |
| **出现文字日志面板**，检查点逐条刷出 | **第 1 步成功** | 拍照，把最后一条检查点发回来 |
| 黑屏 + 可读 panic 文字（含寄存器） | 过墙了，后面某处 panic，**而且读得到了** | 拍整屏，把寄存器和最后几行发回来 |
| 色条一个都没有 | 回退一步 | 查 `logs/payload-patch.log` 探针是否装上；查 `DARWINBOOT:` 行里的 framebuffer 基址和行跨度 |

### 6.2 为什么 `panic_screen` 要保持开启

**【已验证】** `panic_screen_main` 读 `_vinfo` 的 `v_baseaddr`：视频控制台建立**之前**那是
恒等物理地址（已失效，所以现在静默死）；建立**之后**它变成有效的内核 VA，
panic 文字就能正常打出来。**一旦第 1 步生效，`panic_screen` 从累赘变成有用的通道。**

### 6.3 色条含义（官方定义）

**【已验证】** 上游 `docs/zh/guide/debugging.md#颜色条`：

| 色条 | 绘制者 | 含义 |
| --- | --- | --- |
| 绿 | DarwinBoot `DarwinEnter` | 已退出 boot services，准备跳内核 |
| 蓝 | 入口探针阶段 1 | 内核入口已执行，MMU 仍关闭 |
| 品红 | 入口探针阶段 2 | 到达开启 MMU 之前 |

阶段 3 没有色条：它在 MMU 打开后等 `--reset-delay` 秒再 PSCI 复位。
**设备自己重启 = MMU 已打开。**

---

## 第 7 部分　常见失败对照

**【已验证】** 上游 `bring-up.md#失败隔离` 加上本项目实际踩过的：

| 现象 | 先查什么 |
| --- | --- |
| 固件无输出 | 启动镜像 header、固件基址和大小、bootloader 日志 |
| 固件有输出、无菜单 | GOP 驱动、UniXNU 路径、`unixnu.toml` |
| 菜单在、无负载项 | 负载卷标、FAT 驱动、菜单项路径、`[payload] tools` |
| 有负载项、无 `DARWINBOOT:` | 加载器协议、固件卷里的 DarwinBoot |
| `DARWINBOOT: boot refused: <status>` | 内核 Mach-O、ADT、trust cache、占位符。固件会继续运行并回菜单 |
| `NO WINDOW <size> BIG RUN <len> @ <addr>` | 窗口请求大于实际最大连续段 → 调小 `owned_memory_size`，或先做第 0 步测实际值 |
| 内核入口无日志 | 启动参数、framebuffer 地址；当前阶段参考第 5 部分第 1 步 |
| panic 给出 provider 名 | ADT 节点、驱动静态库、内核开关 |
| 无根卷 | userdata 镜像、UUID、分区容量（阶段 6） |
| `unknown build option X` | `unixnu.local.toml` 的键拼错了，允许的键见 2.3 |
| `is not a Mu-Silicium checkout` | `--uefi` 指错了；该目录必须有 `Resources/Scripts/mkbootimg.py` |
| `payload.dram_size differs from the hardware DTS` | **【已验证】** `devicetree.py:306-307` 强制 `[payload] dram_size` 等于 DTS `/memory` 的 size |
| `ADT handoff requires one contiguous DRAM range` | **【已验证】** `devicetree.py:304`：DTS 只能有一个 memory 节点、一段 reg |
| `Owned memory must be 256 MiB to 8 GiB in 2 MiB steps` | **【已验证】** `devicetree.py:320-322` |
| `Pass --unique-chip-id HEX for this board` | `board.toml` 要求平台种子，见 2.3 |
| `<function> has N call(s) to <sym>, not M` | 探针找不到调用点（被内联或改名），换别的符号或位置 |
| `corrupt patch at line N` | 补丁不是干净的 git diff，或行号不匹配 → 按 5.3 在自己的源码树里重新生成 |
| `error: Boot progress requires the audited ...` | 上游门禁。按第 5 部分第 1 步 (a) 处理 |
| 约 118 个未定义符号 | 用了完整版 `BootProgress.c`。按第 1 步 (b) 换 Min 版 |

---

## 第 8 部分　怎么想：三条工作原则

### 8.1 先只读观测，再动手改

本仓库自带原厂数据（`scripts/data/`），很多问题不用猜：

```sh
python3 scripts/read-stock-layout.py          # 原厂内存图 + 原厂 DTB 的外设基址
```

设备上的实测用只读探针（`fastboot boot`，不碰分区）。
**【已验证】** 这也是 `ios-vermeer-lab` 的工作纪律——它每个新文件头部都写着
"read-only"、"gated"、"default-off"、"never changes GCC, SMMU, PMIC or storage"。

### 8.2 分清"已验证"和"别人说的"

本项目前期在这上面浪费了好几轮：A510 匹配、timer 节点、GIC 编码、boot-progress 锁死、
`panic_framebuffer` —— 五个怀疑五个被推翻，原因都是把别人记录里的说法当成了我们这边的事实。

**规矩**：任何来自别人 README/记录的说法，在我们自己的源码或原厂数据里复核之后才能当依据；
复核不了的，在文档里标**【未验证】**。
`docs/playbook.md` 第 6 节有完整的**已排除清单**，上面那五条都在里面，**不要重复排查**。

### 8.3 一次只改一项，并且先问"要不要重编内核"

- 先查第 1.3 节的表：多数诊断改动只需 `payload` + `bootimg`，分钟级
- 一轮内核构建出多个 payload 变体，改名存着连续刷
- 每次刷机留齐第 4.4 节的证据，**尤其是整屏照片和复位秒数**

---

## 第 9 部分　后续阶段的已知待办（现在不做，别忘）

| 项目 | 何时处理 |
| --- | --- |
| **【已验证】** `[userdata]` 的 `first_lba`/`last_lba` 仍是 sheng 占位值（`last_lba = 0x3d0abfa` 超出实测本机 LUN0 末尾 `0x3afa3ff`） | 阶段 6，用 `fastboot oem get-partition-info userdata` 实测替换 |
| **【已验证】** `proprietary-files.txt` 整个是 sheng 的（`@fetch` 指向 `ianchb/sheng-firmware`，条目是 Novatek 平板触摸固件） | 阶段 7。**不要凭空改**：`extract-files.py` 按 SHA-256 校验，条目必须从真机 dump 实测得出 |
| **【已验证】** SoC dtsi 的 GMU 是 sheng 的 `0x3d6a000`，原厂 DTB 是 `qcom,gmu@3d69000` | GPU 阶段。SoC dtsi 是共享仓库，覆盖要在 `dts/vermeer.dts` 做 |
| **【已验证】** 显示后端需要预链接候选 `build/*-prelink/*candidate.macho`（官方 migration 第 7 步列为用户自备输入） | 阶段 7 |
| **【已验证】** 本仓库缺 `porting.toml`，上游 `docs/devices/vermeer-ahzcpp.md` 还是占位页 | 任意时间，补上才会发布真正的子系统记录 |

---

## 第 10 部分　一页速查

```sh
# 一次性准备
sudo scripts/linux/setup-toolchain.py
cp unixnu.example.toml unixnu.local.toml     # 填 device/uefi/dtb/kernelcache/unique_chip_id
./unixnu ipsw fetch

# 完整构建
./unixnu defconfig vermeer_defconfig
./unixnu fetch && ./unixnu tree
./unixnu kernel                               # 慢
./unixnu build package && ./unixnu firmware
./unixnu payload && ./unixnu bootimg

# 只改了 payload 级的东西（探针/参数/窗口/DTS）
./unixnu payload && ./unixnu bootimg          # 分钟级

# 取输出路径
OUT=$(./unixnu status --json | python3 -c 'import json,sys; print(json.load(sys.stdin)["output"])')

# 核对
jq -r .kernel_args "$OUT/payload/payload.json"
cat "$OUT/logs/payload-patch.log"
shasum -a 256 "$OUT/boot/boot.img"

# 刷机（我们的镜像）
fastboot flash boot_b "$OUT/boot/boot.img" && fastboot reboot

# 只读探针（别用 flash）
fastboot boot probe-boot.img

# 原厂硬件数据（在设备仓库里跑）
python3 scripts/read-stock-layout.py
```
