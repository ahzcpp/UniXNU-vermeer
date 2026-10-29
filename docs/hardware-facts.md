# vermeer 硬件实测值与出处

本页只放**有出处的数字**。每条都注明来源，可以自己复核，不要凭别人的 README。

重跑全部内容：

```sh
python3 scripts/read-stock-layout.py
```

它只读 `scripts/data/` 下已在本仓库里的两份原厂数据，不联网、不碰设备：

| 文件 | 是什么 |
| --- | --- |
| `scripts/data/vermeer-uefiplat.cfg` | K70 原厂 UEFI 平台配置，从 OS1.0.x `uefi.img` 提取（FV 在 `0xa7000000`） |
| `scripts/data/vermeer.dtb` | K70 原厂 Android 设备树（510,596 字节） |

---

## 1. 原厂 UEFI 内存图（27 个区域）

**出处：** `scripts/data/vermeer-uefiplat.cfg` 的 `[MemoryMap]` 段。

### 1.1 唯一可供 DarwinBoot 取窗口的内存

DarwinBoot 的 `AllocateWindow` 只能从 `EfiConventionalMemory` 里分配。原厂图里**只有两段**是 `Conv`：

| 基址 | 大小 | 标签 | BuildHob |
| --- | --- | --- | --- |
| `0xbab00000` | `0xd1c0000`（**209.8 MiB**） | DXE Heap | AddMem |
| `0xa6100000` | `0x0f00000`（15.0 MiB） | DBI Dump | NoHob / MMAP_IO |

**最大一段只有约 210 MiB。** sheng 的 `owned_memory_size = 0x130000000`（4.75 GiB）在原厂图下必然 `NO WINDOW`。

### 1.2 本仓库必须对齐的预留区

| 基址 | 大小 | 标签 | 对应 |
| --- | --- | --- | --- |
| `0xb8000000` | `0x2b00000`（43.0 MiB） | **Display Reserved** | `dts/vermeer.dts` 的 `reserved-memory/framebuffer@b8000000` —— **完全一致，已验证正确** |
| `0xa8000000` | `0x10000000`（256.0 MiB） | Kernel | 载荷内嵌内核时用的区域（asteroids 的 `payload_in_kernel` 就放这里） |
| `0xa2a80000` | `0x2b00000`（43.0 MiB） | Display Demura | 另一段显示预留，勿与 splash 混淆 |

### 1.3 原厂图描述的地址上限

`0x100000000`（4 GiB），但最顶上那条是 `0xff800000 + 0x00800000` 的 **NOMAP**；
往下 `0xd8800000 + 0x07400000`（TZApps）结束于 `0xdfc00000`，而 `0xdfc00000`–`0xff800000`
这一段**根本没有被描述**。

---

## 2. 原厂设备树

**出处：** `scripts/data/vermeer.dtb`。

### 2.1 `/memory` 是占位符

```
/memory: reg = 0x0 + 0x0
```

这是高通平台的常态：XBL/ABL 在运行时从 SMEM 读出真实 DDR 布局再填进去。
**所以这个文件无法告出这台机器有多少内存。**

### 2.2 预留区全部在 4 GiB 以下

最高几段：

| 基址 | 大小 | 名字 |
| --- | --- | --- |
| `0xff800000` | 6.0 MiB | `llcc_lpi_region@ff800000` |
| `0xe6440000` | 2.9 MiB | `hwfence-shmem` |
| `0xd8300000` | 5.0 MiB | `qtee_region@d8300000` |
| `0xd4d00000` | 51.0 MiB | `mpss_dsm_region@d4d00000` |
| `0x9ea00000` | 70.5 MiB | `adspslpi_region@9ea00000` |

最高预留地址：`0xffe00000`。**没有任何高位银行的预留。**

### 2.3 外设基址（可用来核对 dts/ 与 SoC dtsi）

| 原厂节点 | 地址 | 我们的 dtsi | 结论 |
| --- | --- | --- | --- |
| `/soc/interrupt-controller@17100000` | `0x17100000` | `gic: interrupt-controller@17100000` | **一致** |
| `/soc/ufshc@1d84000` | `0x1d84000` | `ufs: ufshc@1d84000` | **一致** |
| `/soc/qcom,kgsl-3d0@3d00000` | `0x3d00000` | `gpu: gpu@3d00000` | **一致** |
| `/soc/kgsl-smmu@3da0000` | `0x3da0000` | `gpu_smmu: iommu@3da0000` | **一致** |
| **`/soc/qcom,gmu@3d69000`** | **`0x3d69000`** | `gmu: gmu@3d6a000` | **不一致** |
| `/soc/apps-smmu@15000000` | `0x15000000` | 未描述 | 到存储/GPU 阶段需要 |
| `/soc/ssusb@a600000` | `0xa600000` | 未描述 | DWC3，将来做 USB 控制台用 |

**GMU 地址更正**：原厂 DTB 说是 `0x3d69000`。我们 SoC dtsi 里的 `0x3d6a000` 是 sheng 的值。
另有第三方 README 称是 `0x3d68000`——**三个值都不同，以原厂 DTB 为准**。

这属于 GPU 阶段，不影响当前引导；而且 `gmu` 在 SoC dtsi 里没有 `unixnu,adt-name`，
目前根本进不了 ADT（见 `docs/bring-up.md` 第 6 节）。改之前请先确认，并注意 SoC dtsi 是共享仓库，
设备侧覆盖要在 `dts/vermeer.dts` 里做。

---

## 3. 结论：窗口大小只能实测

三个静态来源**一致地**看不到高位 DRAM 银行：

1. 原厂 UEFI 图最大 `Conv` 段只有 210 MiB，顶部还有未描述空洞
2. 原厂 DTB 的 `/memory` 是 `0 + 0` 占位符
3. 原厂 DTB 的 reserved-memory 全部低于 4 GiB

**所以本仓库里没有任何文件能回答"这台机器能给多大窗口"。** 必须在设备上测。

这和 UniXNU issue #55（nuwa，小米13 Pro，同 SM8550）的说法吻合：
"MemoryMapLib.c 的 DRAM 预留让 `0xBEE00000` 以上全部未映射"（**未验证**，但与上面三条独立一致）。

测法见 `docs/bring-up.md` 第 5 节第 0 步（只读 UEFI 探针，`fastboot boot`，不写分区）。

---

## 4. GPU 观测值

**出处：** `scripts/data/observations/msm-readonly.json`，在本机 Linux 下以**只读**方式从
msm 驱动读出（`kernel_release 7.2.6-sm8550-00103-g42f3b40c702a`，`device_open_mode: read only`，
`set_param: false`，未提交 GPU 任务）。

| 参数 | 值 |
| --- | --- |
| `CHIP_ID` | `0xffff43050a01` → 对应 dtsi 里的 `qcom,adreno-43050a01`（Adreno 740）**一致** |
| `GMEM_SIZE` | `0x300000`（3 MiB） |
| `MAX_FREQ` | 680 MHz |
| `VA_START` / `VA_SIZE` | `0x100000000` / `0xffff00000000` |
| `HIGHEST_BANK_BIT` | 16 |

GPU 阶段用，当前无关。

---

## 5. 固件清单的已知缺陷

**出处：** `proprietary-files.txt` 全文。

该文件目前**整个是 sheng（小米平板 6S Pro）的**：

- 文件头自述 "Proprietary files of the Xiaomi Pad 6S Pro (sheng)"
- `@fetch` 指向 `https://raw.githubusercontent.com/ianchb/sheng-firmware/<commit>/`
- 唯一条目是 `novatek/novatek_nt36532_n81a_fw_csot.bin`，即 **Novatek NT36532、N81A 面板、CSOT** 的
  触摸固件——那是平板的触控器

**未验证** 第三方记录称 K70 的触控器是 Goodix `gt9916S`（SE4，GPIO 44–47，IRQ 25，reset 24），
与 Novatek 不同。原厂 DTB 可以用来确认，方法：

```sh
python3 - <<'EOF'
# 在原厂 DTB 里找触摸相关节点
EOF
```
（或直接用 `scripts/read-stock-layout.py` 的 `fdt_walk` 改一个匹配串。）

**本文不改这个文件。** 原因：`scripts/extract-files.py` 会按 SHA-256 校验每个文件并拒绝其他内容，
而正确的 K70 固件文件及其哈希必须从本机 dump 或可信镜像实测得出，**不能凭空填写**。
UniXNU 的约定也是固件二进制**不进 Git**，只进这份带 `@fetch` 与逐文件 SHA-256 的清单。

到触摸阶段再处理；当前引导阶段不读这个清单。
