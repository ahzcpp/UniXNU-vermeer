# 红米 K70 跑 iPadOS 17.7.11 —— 从零到 r33s 完整经验包

> 目标：在红米 K70（vermeer / 高通 SM8550）上用 **`fastboot boot` 临时启动** iPadOS 17.7.11（21H461，伪装成 iPad7,1），
> 进到**简体中文锁屏 / 桌面**，**触摸屏能用**，全程**不刷任何分区、不碰 abl/xbl**。这份文档把整条路——怎么找、怎么改、
> 怎么写、怎么修触控——从头讲清楚，让别人（哪怕借助 AI）能复刻出和现在一样的效果。
>
> 本成品代号 **r33s**。它 = **r32 的内核**（带显示 + HID + GT9916 触控 + 跳过引导）+ **r33s 变体打包**（启动参数集）。

---

## 目录

0. 先决概念与安全边界
1. 整体架构与启动链
2. 环境和原料（仓库、IPSW、固件、工具链）
3. 内核构建方法论（最关键的"怎么改"）
4. 逐版本撞墙→修复全过程（r4 → r33s）
5. 显示：CPU 渲染、黑背景的真相
6. 触控完整修复（重点中的重点）
7. 跳过引导 + 默认简体中文
8. 打包与临时启动
9. 启动参数总表（逐个解释）
10. 仍然存在的问题
11. GPU（未实验，另见 GPU 包）
12. 给"用 AI 复刻"的人的方法论
13. 文件地图与外部原料
14. 安全须知

---

## 0. 先决概念与安全边界

- **临时启动（temp-boot）**：`fastboot boot boot.img` 只把镜像加载进内存跑一次，**不写任何分区**。重启就回原系统。
  这是整个项目的安全基石——**永远只用它**，绝不 `fastboot flash`。
- **不碰底层分区**：abl、xbl、xbl_config、tz 等**全程不动**。改坏它们手机会变砖/开不了机。我们只临时接管 `boot` 的运行时，不改它的存储。
- **根卷在 userdata**：iPadOS 的系统文件（OS + SystemOS 两个只读卷）写在手机 `userdata` 分区上，格式是自定义的 `SHNGROOT`
  两窗口容器。`boot.img` 里的内核去 userdata 找它。**写 userdata 会清空安卓数据**，所以只在专用测试机上做，且写之前要确认。
- **Data 卷每次全新**：用户数据卷不落盘，用的是打包进 FAT 载荷的一个"种子"，每次开机都是全新的（所以每次都像首刷）。
- **不分发二进制**：苹果的 kernelcache、IPSW 内容、开机 logo，高通/小米的 UEFI/固件，都从**你自己的 IPSW 和你自己手机**里生成，
  不要公开传播；手机序列号等私有信息也不要外泄。

---

## 1. 整体架构与启动链

```
ABL（原厂引导，不改）
 └─ fastboot boot boot.img
     │   boot.img = Android boot header v0：
     │     kernel 区 = BootShim（192B 小跳板）+ Mu-Silicium UEFI 固件镜像（SILICIUM_UEFI.fd, 3MiB）
     │     ramdisk 区 = 128 MiB FAT 载荷（payload.img）
     │     second/dtb = 追加的原厂 v2 DTB（含 splash 保留区节点，否则 ABL 会熄屏）
     └─ Mu-Silicium UEFI（VERMEER_VENDOR_HW=0，只用 Mu 驱动，沿用 ABL 已点亮的屏幕）
         └─ DarwinBoot（UniXNU 的 EFI 加载器）
             │   从 FAT 载荷 EFI/UniXNU/ 读：
             │     Payloads/kernel.macho          ← prelink 过的 XNU
             │     Payloads/kernel.macho.adt       ← Apple Device Tree（改了内存大小等）
             │     Payloads/kernel.macho.trustcache← 信任缓存
             │     Payloads/scratch-data.img       ← 打包的 Data 种子
             │     unixnu.toml                     ← 启动参数、内存窗口等
             │   分配 2 GiB 连续内存窗口给 XNU
             └─ XNU 10063（iOS 内核 10063，UniXNU 打补丁 + 21H461 kernelcache prelink）
                 └─ 原生 UFS 驱动挂载 userdata 上的 SHNGROOT（OS + SystemOS，带 owner 校验）
                     Data 卷 = 内存里的打包种子（每次开机全新）
                     └─ launchd → SpringBoard → 锁屏（简体中文）
```

底层分区全程没动；坏了重启就回 Android / 原 boot。

---

## 2. 环境和原料

### 2.1 主机环境
- **Windows + WSL2（Ubuntu）**：所有编译在 WSL 里做。工程放在 Windows 的 `E:\K70-UniXNU-20261104\`，WSL 里是 `/mnt/e/...`；
  大的构建树放在 WSL 原生盘 `/root/`（避免 `/mnt` 的 I/O 慢和满盘问题）。
- `fastboot`（Android platform-tools）在 Windows 侧，用来临时启动。

### 2.2 代码仓库（都要你自己 clone）
| 用途 | 仓库 |
| --- | --- |
| 内核 / DarwinBoot / prelink 工具 / 设备包 | UniXNU：https://github.com/UniXNU/UniXNU |
| 本机 K70 设备仓库基线 | https://github.com/ahzcpp/UniXNU-vermeer （research/clean-vermeer-20261104 分支，CORE_REVISION 固定到 c1ecc5b…） |
| K70 UEFI 固件 | https://github.com/yyx2025/uefi-vermeer |
| HID 输入层 | https://github.com/UniXNU/ShengHidInput |
| **GT9916 触摸参考**（无许可证，不随包分发，直接看） | https://github.com/xyyyds0106/ios-vermeer-lab （`xnu/vermeer/vermeer_touch.c`、`vermeer_geni_spi.h`、`vermeer_goodix_*.h`、`TOUCH.md`） |
| 参考：另一台机的 sheng 移植（显示/HID/GPU 的"原版"做法） | UniXNU-sheng / UniXNU-sm8550-common |

### 2.3 需要你自己准备的二进制（不在任何包里）
- **iPadOS 17.7.11（21H461，iPad7,1）IPSW**：解出 kernelcache 和两个系统 dmg（OS、SystemOS）做根卷。
- **你自己手机的原厂** `boot` / `vendor_boot` / `dtbo`：取 splash 保留区等板级信息。
- **K70 线刷包**（做 GPU 才需要）里的 `super.img`，提 vendor 固件。

### 2.4 工具链
- UniXNU 自带的 LLVM（`toolchain/llvm/bin/clang-24`）做 iOS 目标编译。
- `mcopy`（mtools）读写 FAT 载荷。
- LLVM 18 的 `llvm-ar` / `llvm-nm` 处理归档和符号。

---

## 3. 内核构建方法论（最关键的"怎么改"）

这是整个项目可复现的核心。**不直接手改 XNU 源码树**，而是：

### 3.1 "派生 + 断言替换" 模式
每一版内核有一对脚本：
- `make-rNN-kernel.py`：**从上一版** `build-r(NN-1)-kernel.py` 读文本，用**精确字符串替换**产出 `build-rNN-kernel.py`。
  每个替换都 `assert` 命中次数（通常恰好 1 次），**改错位置立刻报错**。这样每一版的 diff 极小、可审计、可回溯。
- `build-rNN-kernel.py`：真正干活——从 `compile_commands.json` 取每个源文件的**原始编译命令**，把要改的源文件替换成我们的
  改写版（同样用断言替换注入 C 代码），重编译，然后链接、prelink、补丁、过门禁。

> 关键思想：**我们注入的所有改动都是"对原始源码的精确字符串替换"**，不维护整份改过的源码。别人复刻时读 `make-rNN`
> 就知道每一版到底改了哪一行、为什么。

被重编译的 XNU 源文件（`build-r32-kernel.py` 的 `FILES`）：
```
osfmk/arm/arm_init.c, pexpert/arm/pe_serial.c, pexpert/arm/pe_kprintf.c,
osfmk/console/serial_console.c, osfmk/kern/startup.c,
bsd/kern/bsd_init.c, bsd/kern/kern_exec.c
```
触控、显示、HID、跳过引导的 C 代码分别注入到 `bsd_init.c`（启动探针）、`kern_exec.c`（跳过引导/改偏好/挂载）里，
以及独立编译的 `k70_touch.c`、`K70DisplayProvider.cpp` 等对象再链接进去。

### 3.2 Prelink（预链接）
- 用 UniXNU 的 `emit_prelink.py`，slide = **-0x1000000**。把我们新编的原生内核与 **21H461 的 kernelcache** 对齐，
  用我们的驱动符号替换原镜像里对应的 kext 符号。
- "绑定地址"都是**预链接后地址**（原地址 − 0x1000000）。别人做偏移计算时要注意这个常量。
- prelink 会核对：选中的 Apple 组件集合、原始可执行哈希，与基线计划一致。

### 3.3 Owner Root Trust（能挂载自签根卷的关键）
- `bsd_init` 里：如果苹果的 `FSIOC_KERNEL_ROOTAUTH` 失败，就要求 `native_verified_block_root_authorized(device)` 通过
  （`XNU_OWNER_ROOT_TRUST=1`），否则 panic。
- 根卷的 manifest 用**本地 owner 私钥**签名，内核把它们嵌进 `libVerifiedRootProbe.a`（`inputs.o`，约 73MB，内含签名哈希表 +
  公钥锚 + 原始镜像采样块）。
- Data 卷 UUID 固定在 `CONFIG_XNU_OWNER_DATA_VOLUME_UUID`；复用同一个 Data 种子就一直有效。

### 3.4 最终门禁（硬性自检，防止出废品）
链接产物过 `verify-final-storage.py`：确认无未解析符号、所有自定义符号存在（`_k70_touch_start`、`_native_hid_input_probe`、
`_k70_display_start`…）、GPU 缺席桩存在、两个根卷策略字节与 C 验证输入完全一致、关键 UUID 字符串内嵌等。**任何一条不过就不出镜像**。

---

## 4. 逐版本撞墙 → 修复全过程（r4 → r33s）

这是"怎么找问题、怎么定位"的实战记录。顺序读能理解每一步为什么存在。

| 版本 | 现象 | 根因 | 修法 |
|---|---|---|---|
| r4 | 进到 launchd/SpringBoard，watchdogd 收 SIGTRAP 崩，只剩 2 MiB 内存 | XNU 内存窗口只有 512 MiB | **r5**：UEFI 给 XNU **2 GiB** 窗口（ADT `/chosen/darwinboot-memory-size=0x80000000`） |
| r5/r6 | 内存够了，watchdogd 仍 SIGTRAP → panic | 非苹果硬件没有硬件看门狗 | **r7**：`sheng-disable-watchdogd=1 sheng-lite=1`（launchd 禁掉 watchdogd 和一批联网/iCloud 守护进程） |
| r7 | 用户态在跑，屏幕只有内核文字 | 内核没显示驱动 | **r8**：链入 GOP 旧式显示驱动（NativeLegacyDisplay + K70 启动器），CPU 拷贝到 UEFI 帧缓冲 |
| r9 | `DISPLAY 5 err=5`，SpringBoard 反复 SIGABRT | 显示驱动缺启动参数；缺 AppleKeyStore | 一整套 `offline-legacy-*`；软件 AppleKeyStore（`offline-key-events=1 sheng-aks=1`） |
| r10/r11 | 白苹果卡住 | ADT 缺两项 | ADT `/defaults` 加 `vmm-present=1`（否则 watchdogd 必崩）和 `sleep-disabled=1` |
| r12–r14 | 白苹果，只出几帧 | 看不到卡在哪 | 加**底部状态行** + **按进程崩溃统计表**（诊断屏） |
| r21 | SpringBoard/backboardd 不崩但一直等 | 全新 Data 卷首开跑"升级数据迁移"永远跑不完 | 内核在 Data 挂载后写 `/private/var/mobile/Library/Preferences/com.apple.migration.plist`（`LastSystemVersion=21H461`，属主 501，0600） |
| **r22** | 仍慢，进程被 `0x8BADF00D`（启动超时）杀 | **诊断屏每打一个字符就整块重画（~80 万像素）把 CPU 吃光**；`-enable_kprintf_spam` 雪上加霜 | 状态区每秒最多重画一次；去掉 kprintf 刷屏 → **进 hello 引导页** |
| r23 | hello 页不能操作 | 没有输入设备 | 链入 HID 层（NativeHidInput + HidInputProbe），`offline-hid-auto-home` 自动按 Home → **进语言选择** |
| r24–r32 | 触摸不工作 / 各种崩 | GT9916 在 GENI SPI 上，且是 THP 固件 | 逐步把触控从 GPI 改到 SE DMA、补供电、改主机算触点（见第 6 节），**r32 触控稳定** |
| r28 | 还是平板引导页，小方框语言选择 | 想直接进锁屏 + 简体中文 | `kern_exec` 注入跳过引导 + 改默认语言（见第 7 节） |
| **r33s** | —— | —— | r32 内核 + r33s 变体打包（触控/跳过引导/降透明度等启动参数集），**进简体中文锁屏、触摸可用** |

### 走过的弯路（有价值）
- **USB 串口日志拿不到**：经过 UEFI 后，ABL 留下的 USB 通道（SMMU SID 0x40）没了。试过按 Linux `arm-smmu-qcom` 的 bypass
  怪癖自己重建 context bank（SCTLR.M=0、CBAR S1_TRANS_S2_BYPASS、S2CR TRANS），读回值对，但驱动后面还有 UEFI 布局的 DMA
  检查，放弃。**结论**：要串口日志，更好的路是 ABL→自制加载器（不经过 UEFI，USB 通道天然保留）。
- **在内核日志路径里做"每行字符串匹配检查"** 会把 launchd 前的启动拖死——**诊断代码必须便宜**。
- **黑背景**：见第 5 节。

---

## 5. 显示：CPU 渲染、黑背景的真相

- 没有 GPU，所以用 **CPU 把内容拷到 UEFI 的帧缓冲**。驱动是 UniXNU 的 `NativeLegacyDisplay`（GOP 旧式路径）+ 我们写的
  `K70DisplayProvider.cpp`（源码在 `源码/display/`）。
- 一整套 `offline-legacy-*` 启动参数打开各阶段（注册 / GOP / 单位缩放 / 电源 / 校验 / 传输 / 声明 CPU 在场 / phase=4）。
  `offline-legacy-unit-scale=2` 是 UI 缩放（iPad 最小是 2；3 没验证过，低于 iPad 下限）。
- **为什么背景是黑的**：iPad 设置/锁屏是"居中卡片 + 毛玻璃模糊背景"。没有 GPU，CPU 渲染器画不出模糊，卡片**外面**就是黑的；
  卡片本身的内容、比例、颜色都是对的。
- 缓解：`k70-reduce-transparency=1` 会写入 `com.apple.Accessibility.plist`（`EnhancedBackgroundContrastEnabled`），
  让系统少用透明/模糊，观感好些。**注意**：内核助手**从不覆盖已存在的偏好**——以后 GPU 起来想要模糊，必须去掉这个参数
  **并且**在 Data 里删掉/改回这个偏好。
- 还有个可选的 `k70-fill-backdrop=1`：CPU 把卡片外的黑边填成深蓝（模拟壁纸底色），默认不开。
  **注意**：这段填充代码和 `k70-reduce-transparency` 一样，**不是**写在 `源码/display/K70DisplayProvider.cpp` 里，
  而是构建时由 `build-r32-kernel.py` 用精确字符串替换**注入**进去的（填充注入到 display provider、降透明度注入到 `kern_exec.c`）。
  所以你单独打开那个 .cpp 不会看到它们——要看实现去读 `源码/kernel-build/build-r32-kernel.py`。这就是第 3.1 节"派生+断言替换"模式。
- 另外有个坑：CPU 每帧读非缓存的帧缓冲内存太慢会崩——所以**默认不做全屏回读填充**，用降透明度代替。

---

## 6. 触控完整修复（重点中的重点）

K70 的触摸屏是 **Goodix GT9916**，挂在高通 **GENI SE SPI**（SE4）上，而且固件是 **THP（Touch Host Processing）**——
芯片只吐原始/差分帧，**触点要主机自己算**。这是最难、最花时间的部分。源码在 `源码/touch/`。

### 6.1 传输层：从 GPI 改到 SE DMA
- 一开始走高通 GPI（通用外设接口）DMA，结果：16K 对齐检查失败→放宽到 4K；页不连续→逐页用 `AT S1E1R` 做地址翻译；
  最后 **读 GPI 寄存器直接把 CPU 挂死、系统崩**。
- **换方案**：用 sheng 的 **SE DMA**（`QcomGeniSpi`，在 `源码/touch/qcom-geni-spi/`，原样 vendored，带自己的 LICENSE/SOURCE）。
  SE DMA 模式稳定。SMMU 走 **bypass，SID 0xa3**。`vt_dma` 页用 `AT S1E1R` 翻译成物理地址喂给 SE。

### 6.2 供电：RPMh 投票
- 芯片一开始读回全 0——**电没上**。通过 **RPMh**（`源码/touch/k70_rpmh.h`）对 **APPS RSC 的 drv-2** 投票：
  - `ldob12` → 1.8V
  - `ldob2` → 3.2V
  从 cmd-db 查地址，TCS 写入/触发/轮询，读回确认到 3200mV 才算成功。

### 6.3 芯片 bring-up
- GT9916 **设备确认**（device confirm）。
- 关掉 **HTC**（硬件触点，寄存器 0x90 写 0），因为固件是 THP，触点要主机算。
- `vt_send_cmd`：16 字节命令 + 校验和。还有 `vt_selftest` 自检。

### 6.4 THP：主机侧算触点（核心算法）
固件只在 `frame_data_addr` 放**差分帧**（命令 0x90/0x82）。主机每帧要自己做：
1. 读 18×40 的 **tx-major 矩阵**（`drv`/`sen` = 芯片 IC-info 里的 p[28]/p[29]）。
2. 找**峰值**（局部极大，超过阈值）。
3. 对每个峰做 **3×3 质心**插值，得到亚像素坐标。
4. **slot 跟踪**：把这一帧的触点和上一帧的按距离配对，维持手指 ID（按下/移动/抬起）。
5. 坐标从面板分辨率映射到系统坐标（约 1440×3200 → 30480×20320 的定点坐标）。
- 关键结构 `struct vt_info`（`源码/touch/vermeer_goodix_info.h`）从芯片 IC-info 杂项区解码：
  `cmd +0`、`fw_state +12`、`frame_data +24`、长度 `+28/+30/+32`、`touch_data +44`。
- 可调启动参数：
  - `k70-touch-thr`（峰值阈值，默认 200）——太灵敏/漏触就调它。
  - `k70-touch-map`（坐标映射开关）。
- 状态会打到屏幕底部状态行（方便没有串口时在屏上看触控是否活着）。

### 6.5 接到系统输入
- 算出的触点通过 HID 层的 `native_touch_*` 序列锁 + 通知，喂给 **NativeHidInput**（就是 hello 页/桌面在用的输入设备）。
- `源码/kernel-build/k70-input-glue.c` 是把我们的触控/HID 粘到一起的胶水；`build-hid-objects.py` 构建 HID 对象。

> 复刻触控的最短路径：拿 ios-vermeer-lab 的 GT9916 参考（`vermeer_touch.c` 等），把**传输换成 QcomGeniSpi SE DMA**、
> **补 RPMh 供电**、**保留 THP 主机算点**、**输出改接 NativeHidInput**。我们的 `源码/touch/k70_touch.c` 就是这么来的，可直接读。

---

## 7. 跳过引导 + 默认简体中文

在 `kern_exec.c` 注入两个助手（在 `build-r32-kernel.py` 里以精确替换加入）：
- `k70_skip_setup()`：让系统认为初始设置已完成，直接到锁屏——处理 `purplebuddy`（设置向导）、`notbackedup`、
  `managedconfiguration`、`springboard` 等相关标记/偏好。
- `k70_replace_pref()`：把 `.GlobalPreferences` 的语言设成 **`zh-Hans-CN`**（简体中文）。
- 启动参数 `k70-skip-setup=1` 打开。

> 注意同第 5 节：内核助手**不覆盖已存在的偏好**。因为 Data 种子每次全新，所以每次开机都会写入，语言和跳过引导稳定生效。

---

## 8. 打包与临时启动

1. **内核** → `build-r32-kernel.py` 产出 prelink 过的 `kernel.macho`（约 125MB）。
2. **载荷** → `源码/packaging/build-variant-payload.py`：把内核换进 128MB FAT 载荷模板，改 ADT（`sleep-disabled=1`、
   `vmm-present=1`），设定启动参数，重算 trustcache/Data 种子引用，读回校验每个文件字节一致。
3. **boot.img** → `源码/packaging/package-variant-boot.py`：把 BootShim + UEFI FD + 追加 v2 DTB + 载荷 打成 Android boot
   header v0 的 `boot.img`（约 129.5MB，正好塞进 boot_b 的 192MB），读回校验 FD 和载荷字节一致。
4. **临时启动** → `源码/packaging/boot-variant.ps1` 或成品里的 `临时启动.ps1`：`fastboot boot boot.img`。

r33s 成品的关键哈希：
- `boot.img`：`0fc56179ce9d41f31ad276f057890dbab32dafbdb16ceed4c18589c8016c74d1`（135,774,208 B）
- 载荷内核 `kernel.macho`：`d2ad2673e44f476561241acee9f1bd764fe2d29efe99c54522acec57770873c9`
- FD（Mu UEFI）：`0827f6df15ab79857066c60e2ea11abd4a8cbf63a08de8912b188f212c98ef45`

---

## 9. 启动参数总表（r33s，逐个解释）

```
-v debug=0x14e kt_fixed_zones=120 boot-progress=2 sheng-pmgr=2 -no_slto_panic
native-ufs-root=1 native-ufs-diagnostics=1
sheng-disable-watchdogd=1 sheng-lite=1
k70-display=1 k70-splash=1
offline-key-events=1 sheng-aks=1
offline-legacy-register=1 offline-legacy-gop=1 offline-legacy-unit-scale=2 offline-legacy-pm=1
offline-legacy-validate=1 offline-legacy-transfer=1 offline-legacy-cpu-present=1 offline-legacy-phase=4
sheng-smp=1 armcpu-spi-ipi=1
k70-migration-baseline=1
offline-hid-input=1 offline-hid-home=1 sheng-touch-hid=1
offline-hid-auto-home=30 offline-hid-auto-home-count=1 offline-hid-auto-home-interval=25
k70-touch=1 k70-reduce-transparency=1 k70-skip-setup=1
```
ADT `/defaults`：`sleep-disabled=1`、`vmm-present=1`；`/chosen/darwinboot-memory-size=0x80000000`。

| 参数 | 作用 |
|---|---|
| `-v debug=0x14e` | verbose 内核日志 |
| `kt_fixed_zones=120` | 固定 zone 数，适配受限内存 |
| `boot-progress=2` | 启动进度观察器（屏幕底部状态/诊断） |
| `sheng-pmgr=2` | sheng 电源管理级别 |
| `-no_slto_panic` | 关掉某类超时 panic |
| `native-ufs-root=1` | 用原生 UFS 驱动挂 userdata 根卷 |
| `native-ufs-diagnostics=1` | UFS 诊断输出 |
| `sheng-disable-watchdogd=1` | 禁 watchdogd（没硬件看门狗） |
| `sheng-lite=1` | 禁一批联网/iCloud 守护进程 |
| `k70-display=1` | 打开 K70 显示启动器 |
| `k70-splash=1` | 只显示底部状态条（调显示/ GPU 时可去掉以看更多行） |
| `offline-key-events=1 sheng-aks=1` | 软件 AppleKeyStore |
| `offline-legacy-*` | 旧式 GOP 显示各阶段（注册/GOP/缩放=2/电源/校验/传输/CPU在场/phase=4） |
| `sheng-smp=1 armcpu-spi-ipi=1` | 多核 + SPI IPI |
| `k70-migration-baseline=1` | 写迁移基线 plist（跳过首开数据迁移死循环） |
| `offline-hid-input=1 offline-hid-home=1 sheng-touch-hid=1` | HID 输入设备 + Home 键 + 触控接 HID |
| `offline-hid-auto-home=30 …count=1 …interval=25` | 开机自动按一次 Home（越过 hello） |
| `k70-touch=1` | 打开 GT9916 THP 触控 |
| `k70-reduce-transparency=1` | 降透明度（缓解黑背景） |
| `k70-skip-setup=1` | 跳过初始设置向导 + 默认简体中文 |

可调：`k70-touch-thr=200`（触控阈值）、`k70-touch-map`（坐标映射）。

---

## 10. 仍然存在的问题（交接给下一个人）

1. **熄屏不回锁屏**：电源键事件还没接进来。要在 HID/电源路径里补电源键 → 触发锁屏/亮灭屏。
2. **桌面比例 / 图标偏大**：`offline-legacy-unit-scale=2`。3 没验证过且低于 iPad 下限，要试要小心。
3. **小组件白块 / 模糊白 / 黑背景**：需要 **GPU**（Adreno 740 + Metal）。见第 11 节与 GPU 包。
4. **无充电驱动**：跑 iPadOS 会掉电，测完回 fastboot 充。

---

## 11. GPU（未实验）

GPU（Adreno 740 / Metal）的**离线准备**已完成（内核侧能编译/链接/prelink），但**从没上过机**，r33s 没用它。
详见交付包 `GPU驱动-未实验/`：
- 内核侧：`libAdreno740.a` + `libK70Gpu.a`，已对 r32 内核链接闭包 0 未解析符号；GPU 版内核 r32-gpu 离线全通过，未打 boot.img。
- **阻塞**：用户态 Metal 栈（Turnip + Metal 插件 + gpuaddon + owner trust cache）**必须在 macOS 上构建**。
- 真机第一次试：在 r33s 参数上追加 `sheng-gpu-power=3`，逐级到 `=9`（分级表见 GPU 包 README 第 7 节），**从最低档开始**。

---

## 12. 给"用 AI 复刻"的人的方法论

如果你（或你让的人）用 AI（甚至同一个模型）重做，按这套来，能复刻出同样效果：

**怎么找**
- 先读本经验包和 `源码/`，再读各上游仓库。设备相关的值（寄存器、SID、供电轨、面板参数）优先去**手机原厂设备树**（`stock.dts` / dtbo）里核对，不要凭空编。
- 定位某个功能在哪：在内核树里 `grep` 功能符号名（如 `native_hid_input`、`native_verified_block_root_authorized`、
  `offline_legacy_*`）和启动参数字符串（`PE_parse_boot_argn("k70-...")`）。
- 比对两个 iOS 版本的函数/字段：用**掩码指令匹配**（把立即数掩掉比对字节）、**vtable 槽映射**、**调用点经 GOT 桩映射**、
  **同族 vtable 查找**——这些在 ios16-next 的 remap 脚本里有现成实现，可借鉴。

**怎么改**
- 坚持"**派生 + 断言精确替换**"：新版从旧版 `build` 脚本文本派生，每个替换 `assert` 命中次数。改错立刻炸，diff 可审计。
- 不手改 XNU 整树；要改的源文件从 `compile_commands.json` 取原始编译命令重编译，注入我们的 C 代码。
- 所有行为用**启动参数开关**控制（`PE_parse_boot_argn`），默认关，便于逐级排错。

**怎么写**
- 诊断代码必须**便宜**（第 4 节的教训）：不要在每行日志/每帧做重活。屏幕状态每秒最多重画一次。
- 新符号要进第 3.4 节的**最终门禁**自检列表，保证没链漏。

**怎么修触控**（最常被问）
- 认清是 **THP** 固件：芯片只给差分帧，触点主机算。
- 传输用 **QcomGeniSpi SE DMA**（别碰 GPI，会挂 CPU）。SMMU bypass，SID 0xa3，页用 AT 翻译。
- 先**供电**（RPMh 投 ldob12=1.8V / ldob2=3.2V，读回确认），再 **device confirm**、**关 HTC(0x90=0)**。
- 主机算点：18×40 矩阵 → 找峰 → 3×3 质心 → slot 跟踪 → 映射坐标 → 接 NativeHidInput。阈值 `k70-touch-thr` 可调。

---

## 13. 文件地图与外部原料

### 本交付包
```
K70-r33s-交付包\
├─ 00-总览-先读我.md
├─ 成品\
│   ├─ boot.img                 r33s 成品（临时启动用；sha 0fc56179…）
│   ├─ boot.img.sha256.txt
│   ├─ 临时启动.ps1             一键 fastboot boot（自动校验/认设备）
│   └─ 刷机说明-给小白.md
├─ 经验包\
│   ├─ K70-iPadOS-r33s-完整教程.md   （本文件）
│   └─ 源码\
│       ├─ touch\               GT9916 THP 触控（k70_touch.c + 头 + vendored QcomGeniSpi）
│       ├─ display\             K70DisplayProvider.cpp + 构建/校验脚本
│       ├─ kernel-build\        make-r32 / build-r32 内核脚本、HID 构建、输入胶水
│       └─ packaging\           载荷/boot.img 打包、临时启动脚本（序列号已去除）
└─ GPU驱动-未实验\
    ├─ 未实验-务必先读.md
    └─ gpu-next\                GPU 离线工程（未上机）
```

### 外部原料（自己拿）
见第 2.2 / 2.3 节的仓库与 IPSW/固件清单。触控参考 ios-vermeer-lab 无许可证，不随包分发，请直接去看。

---

## 14. 安全须知

- **只用 `fastboot boot` 临时启动**；**绝不刷 abl/xbl 等底层分区**，否则可能变砖。
- 写 userdata（装根卷）会**清空数据**，只在专用测试机上做，动手前先确认备份。
- `boot.img` 内嵌苹果内核与 OEM 固件（都来自你自己的 IPSW/手机）——**别公开传播**；别外泄手机序列号等私有信息。
- 系统无充电驱动，跑 iPadOS 会掉电，测完回 fastboot 充电。
