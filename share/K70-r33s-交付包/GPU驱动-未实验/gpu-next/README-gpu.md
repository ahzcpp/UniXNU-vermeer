# Redmi K70 (vermeer, SM8550) — Adreno 740 / Metal GPU 移植准备（iPadOS 17.7.11 21H461）

> 全部为**离线**工作：没有运行 fastboot、没有接触手机、没有写任何分区。
> Apple / Qualcomm / 小米二进制只放在 WSL `/root/k70-gpu-work/private/`，不进 git（本目录有 `.gitignore`）。

## 1. 结论速览

| 项目 | 状态 |
| --- | --- |
| K70 GPU 板级输入（寄存器/IRQ/SMMU SID/OPP/带宽/cmd-db/zap 保留区） | ✅ 与 sheng 用的主线 SM8550 值**完全一致**，18/18 项对 K70 stock DT 核对通过 |
| "GMU 窗 0x3d68000 vs 0x3d6a000"（yyx2025 §4.4） | ✅ 不是硬件差异：kgsl 从 0x3d68000 描述（长 0x37000），主线从 0x3d6a000（长 0x35000），两者都止于 0x3d9f000；生成头里的寄存器偏移相对主线起点，物理地址完全相同 |
| 生成的 GMU/投票/cmd-db/SMMU 头文件 | ✅ `generated/`，与 sheng 原版生成器结果**逐字节相同** |
| 固件 `gmu_gen70200.bin` | ✅ 取自 K70 vendor，哈希与 UniXNU 固定值、linux-firmware 一致 |
| 固件 `a740_sqe.fw` | ✅ 使用 UniXNU 固定的通用版本（75380 B）；K70 vendor 自带的是更旧的 74804 B 版本，未采用 |
| 固件 `a740_zap.mbn`（K70 自己签名） | ✅ 从 K70 线刷包 super.img → vendor_a → `/firmware/a740_zap.mdt+.b00-.b02` 拼成 12088 B，与 sheng 的 zap 哈希不同（确认是本机签名件） |
| 内核侧 `libAdreno740.a`（驱动+GMU/HFI/SMMU/PM4 kits+固件+加速器） | ✅ 针对 K70 的 iOS 17 内核树 `-Werror` 编译通过 |
| 内核侧 `libK70Gpu.a`（sheng `GpuPowerProbe.cpp` + K70 胶水） | ✅ 编译通过；对 r32 原生内核链接闭包 0 个未解析符号 |
| GPU 版完整内核（r32-gpu：链接+prelink+入口/panic 补丁+存储硬门） | ✅ 离线全部通过，见 §5（未打 boot.img、未上机） |
| 用户态：Turnip / NativeAdrenoMetal 插件 / gpuaddon.bin / owner trust cache | ❌ 阻塞：需要 macOS（Xcode iPhoneOS SDK、Rust、meson、glslang、Apple codesign），见 §6 |
| 真机验证 | ❌ 未做（按要求不碰手机） |

## 2. K70 与 sheng 的 GPU 输入对比

来源：K70 stock DT `/root/k70dt/stock.dts`（kgsl 下游描述）、dtbo `53.dts`（vermeer overlay，未覆盖任何 GPU/保留区节点），
对照 UniXNU 固定的主线 `sm8550.dtsi`（ianchb/sm8550-mainline@42f3b40）。完整结果：`reports/k70-gpu-dt-crosscheck.json`。

| 输入 | K70 stock（kgsl） | 主线 / sheng | 结论 |
| --- | --- | --- | --- |
| GPU 核心寄存器 | 0x3d00000 / 0x40000 | 同 | 相同 |
| cx_misc / cx_mem | 0x3d9e000 / 0x1000 | 同 | 相同 |
| cx_dbgc | 0x3d61000 / 0x800 | 同 | 相同 |
| RSCC | 0x3d50000 / 0x10000 | 同 | 相同 |
| GMU PDC | 0xb280000 / 0x10000 | 同 | 相同 |
| GMU | 0x3d68000 / 0x37000 | 0x3d6a000 / 0x35000 | 同一块，止于 0x3d9f000 |
| GPU IRQ | SPI 300 | 同 | 相同 |
| GMU IRQ (hfi, gmu) | SPI 304, 305 | 同 | 相同 |
| Adreno SMMU | 0x3da0000 / 0x40000 | 同 | 相同 |
| GPU SID | gfx3d_user 0, gfx3d_lpac 1（secure 2 不用） | 0, 1 | 相同 |
| GMU SID | 5 | 5 | 相同 |
| ZAP 保留区 | 0x9b09a000 / 0x2000 no-map | 同 | 相同 |
| cmd-db | 0x81c60000 / 0x20000 | 同 | 相同（运行时从此处读 BCM/ARC 地址） |
| GPU OPP | bin-0（sku 0x3,0x6,…）：719/680/615/550/475/401/348/295/220 MHz；bin-1（sku 0）：680…220 | 680…220 MHz（无 719） | 主线 8 档的频率/RPMh 等级/ACD 与两个 bin 完全吻合；719 MHz@SVS_L2 仅 bin-0 有，未启用（安全） |
| GMU OPP | 220 MHz@LOW_SVS、550 MHz@SVS | 200 MHz@LOW_SVS、500 MHz@SVS | 同等级不同频点；沿用 sheng 已真机验证的主线值 |
| 带宽 | DDR 总线表 10 档 | 每个主线 peak-kBps 都是 K70 总线表里的一项 | 兼容 |
| GPU 电源 | GX/CX/MX/MXC 走 RPMh ARC（无板级 PMIC 差异）；gpucc 0x3d90000，GCC 0x100000 | 同 | SoC 级，相同 |

所以 K70 唯一板级差异是**签名 zap**。`board-dts/sm8550-xiaomi-vermeer.dts` 只做两件事：启用 `&gpu`、把 zap 名设为
`qcom/sm8550/vermeer/a740_zap.mbn`。

## 3. 固件（不入 git）

| 文件（overlay 键名） | 字节 | SHA-256 | 来源 |
| --- | --- | --- | --- |
| `qcom/gmu_gen70200.bin` | 66844 | `1a2a419c39046d3141fc5fed5aa7f971de2db40cc7a1d89693c3e26fad64dd98` | K70 vendor_a `/firmware/gmu_gen70200.bin`（= linux-firmware = UniXNU 固定值） |
| `qcom/a740_sqe.fw` | 75380 | `6d38046654333a6a5a4ee77ab67abc24e3c0c5c68237513afe98e94da8fc9fcc` | UniXNU 固定的通用 SQE（git blob ac5758bb…） |
| `qcom/sm8550/vermeer/a740_zap.mbn` | 12088 | `b1543574ce8924ff42653849f7d4a7949215821515f1b59444e3f0e99e0a798d` | K70 vendor_a `/firmware/a740_zap.mdt` + `.b00/.b01/.b02` 拼接（`scripts/squash-mdt.py`） |

- 来源包：`E:\卒\os1.7\vermeer_images_OS1.0.7.0.UNKCNXM_14.0\images\super.img`（vendor 指纹
  `Xiaomi/mivendor_sm8550_cn/mivendor:13/TKQ1.221114.001/V816.0.7.0.UNKCNXM`）。
- 存放：WSL `/root/k70-gpu-work/private/firmware/qcom/...`，overlay 树里以 `.cache/firmware/vermeer-gpu` 符号链接引用。
- `vendor_a.img` 中间文件（~800 MB）已删除，可随时用下面命令重做。
- **风险**：手机当前基带/TZ 若是 OS2.x（`firmware-inputs\fw_vermeer…OS2.0.218.0…zip`），而 zap 来自 OS1.0.7.0。zap 由同一 OEM
  密钥签名，通常可被接受；如果 TZ 因防回滚拒绝（GpuPowerProbe 的 Z 行 `a<非0>`），需要 OS2.0.218 对应的 **vendor** 分区
  （完整 OTA 或线刷包里的 super.img / vendor.img，`fw_…zip` 里没有 vendor），用同样脚本重新提取 `a740_zap.*`。

重新提取：

```bash
python3 -I /mnt/e/K70-UniXNU-20261104/gpu-next/scripts/extract-super-partition.py "/mnt/e/卒/os1.7/vermeer_images_OS1.0.7.0.UNKCNXM_14.0/images/super.img" vendor_a /root/k70-gpu-work/private/vendor_a.img
```

```bash
python3 -I /mnt/e/K70-UniXNU-20261104/gpu-next/scripts/erofs-extract.py /root/k70-gpu-work/private/vendor_a.img /root/k70-gpu-work/private/vendor-x /firmware
```

```bash
python3 -I /mnt/e/K70-UniXNU-20261104/gpu-next/scripts/squash-mdt.py /root/k70-gpu-work/private/vendor-x/firmware/a740_zap.mdt /root/k70-gpu-work/private/firmware/qcom/sm8550/vermeer/a740_zap.mbn
```

（`erofs-extract.py` 是纯 Python 只读 EROFS 解析器，支持该 vendor 镜像用到的 lz4+0padding / compact 索引。）

## 4. 内核侧构建（已完成）

一条命令重建全部内核侧 GPU 对象（WSL，约 2 分钟，不接触手机）：

```bash
bash /mnt/e/K70-UniXNU-20261104/gpu-next/scripts/build-k70-gpu-kernel-objects.sh
```

它做的事：

1. `setup-gpu-tree.sh`：在 `/root/k70-gpu-work/tree` 建叠加树——只读链接 K70 内核树的 `build/xnu`、`.cache/xnu-work`、
   UniXNU 检出的 `drivers/Adreno740`、`kits/{AdrenoFirmware,AdrenoPM4,ArmSmmu4K,…}`、sm8550-common 的 `kits/Sm8550Platform`；
   `kits/vermeer` = sheng `package/` 的拷贝 + 本目录 `board/` 文件。
2. `fetch-k70-gpu-sources.py`：只下载 UniXNU 固定 SHA 的主线 Linux a6xx/DT 源码（**不下载任何固件**）。
3. `derive-vermeer-generators.py`：用精确替换把 sheng 的 `audit-adreno-platform.py`、`prepare-sheng-gmu*.py`、
   `prepare-sheng-gpu-smmu.py`、`build-adreno740-kits.py` 派生成 vermeer 版（换 DTS/DTB/zap 名，匹配次数不对就失败）。
4. 生成头：`build/sheng-gmu/{ShengGmuSource.h,ShengGmuVotesSource.h,ShengGmuCommandDbSource.h}`、
   `build/sheng-gpu-smmu/ShengGpuSmmuSource.h`（副本在 `generated/`）。
5. `build-arm-driver.py Adreno740` → `NativeAdreno.o`；`build-vermeer-adreno740-kits.py` → 44 个 kit 对象 + 固件 `.incbin`
   → `build/adreno740/libAdreno740.a`；`build-adreno740-accelerator.py` → 暂存的 AGXAccelerator（也追加进 libAdreno740.a）。
6. `build-k70-gpu-power-probe.py` → `GpuPowerProbe.o`；`build-k70-gpu-glue.py` → `build/k70-gpu/libK70Gpu.a`。
7. `gpu-link-closure.py`：对最新 rNN 原生内核做符号闭包（`reports/gpu-link-closure.json`）。

K70 专门加的适配（都在 `board/`、`scripts/`，不改上游源码）：

- `board/K70XnuCompat.h`：K70 内核的 XNU（iOS 17 源）没有 `task_get_gpu_role` / `PRIO_DARWIN_GPU_BACKGROUND…`。
  兼容头让角色恒为"未知"，即**所有提交都用队列的前台优先级**（文档中"unspecified role"行为）。仅注入 C++ 驱动对象。
- `board/k70_gpu_glue.c`：sheng 专有钩子在 K70 上的"缺省"实现——`boot_latency_*`（关）、`boot_progress_ext_wide`（两行宽 GPU 状态缓冲）、
  `boot_log_export_put`（拒绝，管线缓存不导出）、`native_pmap_user_mappings`（sheng 27 系列的 XNU 补丁，返回 0，与驱动的 weak 回退等价）、
  `native_gop_handoff_*`（NativeGOPPresenter 用户态扫描输出交接，返回 Unsupported；K70 走 NativeLegacyDisplay + K70DisplayProvider）。
- `scripts/brew-shim.sh` + `scripts/llvm-objcopy-shim.py`：上游脚本在 macOS 上调用 `brew --prefix llvm` 和
  `llvm-objcopy --remove-section=__DATA,__mod_init_func --globalize-symbol=…`；LLVM 18 的 Mach-O objcopy 不支持后者，
  改用本工程 `display-next/reference/coachz_late_init.stage_initializer`（r 系列已用于 NativeKeyStoreEvents，真机验证过）。

已确认 K70 内核**已经带** UniXNU 的 `zzy-native-gpu-addon-mount.patch`（`native_gpu_addon_*` 符号在 r32 中存在），
`sheng-gpu-addon=1` 时会把 Data 卷里的 GPU addon 挂进系统路径，用户态 addon 不需要再改内核。

## 5. GPU 版内核（派生脚本）

`scripts/make-gpu-kernel.py` 从任意 `usb-next/build-rNN-kernel.py` 精确派生出 GPU 版，只改 4 处：

1. 输出目录 → `/root/k70-gpu-work/kernel-rNN-gpu`，结果 JSON → `gpu-next/kernel/`（不写主线的 usb-next 结果和 storage-candidate/rNN）；
2. `bsd_init.c`：在 `native_keystore_probe()` 后调用 `sheng_gpu_power_probe()`（与 sheng 的 late probe 顺序一致；未设 `sheng-gpu-power` 时什么都不做）；
3. 链接：去掉 `gpu-absent.o`，force_load `libAdreno740.a` 与 `libK70Gpu.a`（它们提供显示归档 import 的 fence/trace API）；
4. 最终符号检查额外要求 `_sheng_gpu_power_probe` 和三份内嵌固件符号。

```bash
cd /mnt/e/K70-UniXNU-20261104 && python3 -I gpu-next/scripts/make-gpu-kernel.py usb-next/build-r33-kernel.py gpu-next/kernel/build-r33-gpu-kernel.py
```

```bash
cd /mnt/e/K70-UniXNU-20261104/gpu-next/kernel && python3 build-r33-gpu-kernel.py
```

本次以 r32 为底的结果（`kernel/r32-gpu-kernel-result.json`）：编译 → 链接 → prelink（Apple 组件与 r32 计划一致）→ 入口/panic 补丁
→ 存储硬门 → 最终符号检查（含 `_sheng_gpu_power_probe` 与内嵌的 GMU/SQE/ZAP 映像）**全部 PASS**，状态
`compiled_prelinked_probed_storage_verified_not_device_tested`：

- 内核：WSL `/root/k70-gpu-work/kernel-r32-gpu/kernel-payload.macho`，125501440 B，
  SHA-256 `1b45e633c2dc90650e0247f88983bd2510c6e05f4560b8e54a9d7312af871342`；
- `bsd_init.c` 中 `sheng_gpu_power_probe()` 紧随 `native_keystore_probe()`；不加 `sheng-gpu-power` 时与 r32 行为相同（探针直接返回，
  fence 查询由真驱动回答，不再是 gpu-absent 桩）。
- **还没有打 boot.img**：打包属于主会话的 variant 流程（`build-variant-payload.py` / `package-variant-boot.py`），
  需要把这个 kernel 换进 payload 再按 §7 的 G1 参数 temp-boot。主线已到 r33s，建议用上面的命令从最新 rNN 重新派生。

## 6. 阻塞项与需要你提供/决定的东西

1. **用户态 Metal 栈需要 macOS**（硬阻塞）。iOS 17 的三步：
   `build-turnip-darwin.py --target ios`、`build-adreno-metal-plugin.py --target ios`、`make-gpu-addon.py --publish build/gpuaddon.bin`。
   需要：Xcode 的 iPhoneOS SDK（Metal/Foundation/IOSurface 框架，WSL 里只有内核用的 fakeroot SDK）、Rust（`aarch64-apple-ios`，
   metal2vulkan 桥）、meson + Mesa 固定提交 + venv（mako/pyyaml）、glslangValidator、真正的 Apple `codesign`（WSL 的是空壳），
   以及 `make-owner-trust-bundle.py` 生成的 owner trust cache。插件/Turnip 不含板级内容，**与 sheng iOS 17 产物相同**——
   只要 UniXNU 版本一致（本次用的是 K70 检出 c1ecc5b，Adreno740 6fa7bae / AdrenoMetal 243d63c），在任一台 Mac 上按 sheng 流程构建即可。
2. **内核用的 XNU 比 Adreno740 预期的旧**：没有 per-task GPU 角色（见 §4 兼容头）。若以后 K70 换到带 `resource_private.h` GPU 角色的 XNU 源，删掉 `-include K70XnuCompat.h` 即可。
3. **zap 防回滚风险**（§3）。如果 TZ 拒绝，需要 OS2.0.218 的 vendor 分区镜像。
4. **NativeGOPPresenter**：当前用胶水桩禁用；若要走 sheng 的 GOP 用户态扫描输出路径，改为构建
   `build-arm-driver.py NativeGOPPresenter` 的 late 对象并去掉胶水里的两个 `native_gop_handoff_*`。
5. **透明度/模糊**：r27 起的 `k70-reduce-transparency=1` 会写入 `com.apple.Accessibility.plist`（`EnhancedBackgroundContrastEnabled`），
   且内核助手**从不覆盖已存在的偏好**。GPU 起来后想看到模糊，要去掉该启动参数**并**在 Data 里删除/改回这个偏好。
6. GPU 状态显示：GpuPowerProbe 把结果写进 `boot_progress_ext` 第 4–7 行和 `boot_progress_ext_wide`；`k70-splash=1` 时只显示底部状态条，
   调 GPU 时建议去掉 `k70-splash=1` 或让观察器显示这几行。

## 7. 以后真机要试的启动参数（逐级，按 sheng `sheng-gpu-power` 分级）

在当时最新的 K70 变体参数基础上（例如 r33s），只**追加**下面的项；每一级都是上一级的超集，失败就停在上一级：

| 轮次 | 追加参数 | 期望 |
| --- | --- | --- |
| G1 | `sheng-gpu-power=3` | 只读 GCC/GPUCC/SMMU，打开 CX GDSC 和 SMMU 投票时钟；不碰 GX |
| G2 | `sheng-gpu-power=5` | 建 GMU SMMU 上下文；只读 cmd-db（0x81c60000）并生成 HFI 投票表（行 7 `p0 s0`） |
| G3 | `sheng-gpu-power=7` | GMU 时钟 + GMU 冷启动（gmu_gen70200.bin，HFI 就绪） |
| G4 | `sheng-gpu-power=8` | HFI 调频 + OOB GPU_SET，GX 上电 |
| G5 | `sheng-gpu-power=9` | hw_init → SQE → CP_ME_INIT → 两次 fence → **K70 zap**（Z 行 i/m/a 都应为 0）→ 退出安全模式 |
| G6 | `sheng-gpu-power=9 sheng-gpu-metal=1 sheng-fb-gpu=1 sheng-gpu-dcvs=1 sheng-gpu-addon=1` + payload 带 addon | 发布 Metal 设备，桌面走 GPU 合成 |

G6 的打包（UniXNU payload 步骤的标志，K70 的 variant 打包器需要把它们传给 `unixnu payload`）：

```bash
./unixnu payload -D <vermeer device> --gpu-addon build/gpuaddon.bin --owner-trust-cache build/owner-trust-cache.bin
```

sheng iOS 17 配置里与 GPU 相关的完整集合为
`sheng-gpu-power=9 sheng-gpu-metal=1 sheng-fb-gpu=1 sheng-banner=0 sheng-gpu-dcvs=1 sheng-gpu-addon=1`；
`sheng-gpu-preempt` 默认 2（双 ring），出问题可试 `sheng-gpu-preempt=0`；`sheng-gpu-zap=0..4` 可把 zap 停在
init_image / mem_setup / auth_and_reset 之前用于排错。保留现有 Data seed，让持久数据与缓存身份不变。

## 8. 目录

| 路径 | 内容 |
| --- | --- |
| `scripts/` | 全部脚本（提取、生成、派生、构建、核对、内核派生器） |
| `board/` | `K70XnuCompat.h`、`k70_gpu_glue.c`（拷进 overlay 的 `kits/vermeer`） |
| `board-dts/sm8550-xiaomi-vermeer.dts` | K70 主线格式 GPU 板级 DTS |
| `data/adreno-inputs.vermeer.json` | UniXNU 固定输入 + K70 固件哈希/来源 |
| `generated/` | 生成的 GMU/投票/cmd-db/SMMU 头文件与报告（只有源码推导的常量和固件哈希） |
| `reports/` | `k70-gpu-dt-crosscheck.json`、`gpu-link-closure.json` |
| `kernel/` | 派生的 GPU 版内核构建脚本与结果 |
| WSL `/root/k70-gpu-work/` | overlay 树、构建产物、日志、`private/`（固件，不公开） |
