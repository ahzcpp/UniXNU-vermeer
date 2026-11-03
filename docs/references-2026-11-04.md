# K70 / vermeer 原始参考资料核对（2026-11-04）

本报告只记录文档和源码检查。用户已明确：两张照片均为参考图片，不能作为本机实测；当前 K70 只有引导状态。旧目录、旧日志以及其他维护者的设备观测均不计为这台手机的验证。没有执行设备命令、刷分区或上传系统镜像。

## 干净起点与推荐路线

本轮目标源码为 `/root/k70-clean-20261104/device`（ahzcpp/UniXNU-vermeer 的 main，f021 前缀）和 `/root/k70-clean-20261104/UniXNU`（c1ecc5b74c9dc4ced13bcfbd81a2c3998025a5e5）。原厂 boot/vendor_boot 只用于本机硬件输入与恢复依据，不能单独构成 iOS 移植。完整 UEFI 还需要该机型的厂商固件模块；完整用户态还需要所选 OS profile 的 kernelcache/根镜像、trust cache、Data seed 及其 ABI 输入。

建议保持这套干净源码，先获得 UEFI → UniXNU 菜单 → DarwinBoot → 最小 XNU 平台输出的可审计链路，再接存储、根卷与用户态。保持各层精确版本，不把多套项目的内核、loader、ADT、固件和启动参数混装。[官方架构](https://unixnu.com/porting/)说明的层次是 BootShim、Mu UEFI、UniXNU、DarwinBoot、XNU、目标系统用户态。

[官方 vermeer 记录](https://unixnu.com/devices/vermeer)目前只报告社区的 CI 平台内核构建和另外一次 UEFI 菜单验证；正文明确 XNU 的手机执行待验证。[ahzcpp 页面](https://unixnu.com/devices/vermeer-ahzcpp)要求完成 porting.toml，未发布子系统记录。官网列表的级别不是进入 iOS 的证据。

## 直接影响构建与定位的约束

- [移植步骤](https://unixnu.com/zh/porting/steps)要求目标设备的硬件资源与分区输入、固定组件、独立 firmware DTB 和 XNU ADT。板级数组替换继承数组，必须保留完整依赖。寄存器、保留区、触控线路与分区布局不能照搬 sheng/fuxi。
- [设备仓库](https://unixnu.com/zh/porting/device-repository)使用格式 2、board.toml、feeds、lock、defconfig、DTS、补丁序列、porting.toml。源码实现、主机构建和真机验证分开记。构建成功仅支持主机阶段结论。
- [打包规范](https://unixnu.com/zh/porting/packaging)指定原样 templates/package.sh，通过 TOML 提供板级数据，依次 deps/preflight/patches/build/pack/verify。产物需要清单和哈希；OS 差异使用 os_overrides，不能只靠修改脚本硬编码。
- [固件集成](https://unixnu.com/zh/porting/uefi-firmware)要求 UniXNU、UniXNUDxe、DarwinBoot、UniXNURamDiskDxe、DiskIoDxe、FAT 进入 DSC/FDF。菜单有启动项不等于 loader 已进入 XNU。FD 大小、基址、BootShim header 和原厂 bootloader 格式均需匹配。
- [设备树](https://unixnu.com/zh/porting/device-tree)中 `/chosen`、`/cpus`、`/arm-io`、`/defaults`、`/options`、NVRAM proxy 必须有效；有 ramdisk/trust cache 就需要相应占位。`darwinboot-memory-size` 是连续窗口请求，允许 256 MiB 至 8 GiB、2 MiB 步进；实际可用范围要看 UEFI 内存图。
- [启动参数](https://unixnu.com/zh/xnu/boot-arguments#apply-arguments)按板级→本地→CLI 同名覆盖，限制 1000 字节。修改后重建 payload 和 bootimg；只改外部 TOML 不会更新已经打包的 boot.img。核对 payload 的 args 与 DarwinBoot 打印的命令行。
- [payload](https://unixnu.com/zh/concepts/payload)是 FAT ramdisk，保存内核、相邻 ADT、配置及所需缓存/种子。启用 OWNER_DATA_MOUNT 时缺 Data seed 应在构建期失败。
- [userdata 布局](https://unixnu.com/zh/concepts/userdata)以 SHNGROOT manifest 映射 OS/SystemOS 窗口，卷 UUID、偏移、容量和实际镜像必须一致；Data 是独立种子/覆盖层。临时 `fastboot boot` 只描述启动镜像加载方式，不自动保证运行中的内核和用户态不写存储。
- [bring-up](https://unixnu.com/zh/porting/bring-up)依次确认 UEFI、菜单、payload、DarwinBoot、XNU 平台、根卷、用户态；每次保留最后完成的阶段。无根卷先查 userdata、UUID、窗口偏移与容量。
- [调试文档](https://unixnu.com/zh/guide/debugging)要求保存新镜像哈希、日志与检查点。绿条是 ExitBootServices 后的 loader 交接，蓝/品红入口探针和 MMU 后定时 reset 能缩小停点；仅凭绿条不能认定执行到 XNU。USB 日志工具需设备实现兼容协议，ADB 也不会自动存在于早期 UEFI/XNU 阶段。

## Linux XNU 工具链核对

已直接读取两个固定版本的 setup-toolchain.py；[066c8d](https://github.com/UniXNU/UniXNU/blob/066c8d8315f83749d28bcae2faf3595718646be5/scripts/linux/setup-toolchain.py) 与 [c1ecc5b](https://github.com/UniXNU/UniXNU/blob/c1ecc5b74c9dc4ced13bcfbd81a2c3998025a5e5/scripts/linux/setup-toolchain.py)文本完全相同，均 20620 字节。两者均：

- 固定 LLVM `58c46bae21181fb4240cf964461bea738d96d50e`，应用 `llvm-xnu-builtin-type.patch`，构建 AArch64 clang 及 resource headers。
- 检查 `__builtin_xnu_type_summary`，不是仅检查 clang 文件存在。系统 clang18 的软链接不满足这个检查。
- 固定 cctools `1030.6.3-ld64-956.6`、bootstrap_cmds-138、iig-port `ea6d147d668ca15d490da638012243fcd705f30d`，并提供主机与 xcrun/plutil/codesign 等配套布局。

截至本次查询，[UniXNU releases API](https://api.github.com/repos/UniXNU/UniXNU/releases?per_page=10)与 [Allenwdk releases API](https://api.github.com/repos/Allenwdk/xnu-linux-toolchain/releases?per_page=20)均返回空数组，未发现这两仓库发布的预编译正确 XNU LLVM。不能据此排除未公开的个人缓存，但不能把未知缓存当官方工具链。Linux CI 缓存需要与固定 LLVM、补丁内容和主机架构绑定；恢复缓存后运行 --check 和 builtin 检查，失败即重建。旧系统 clang18 缓存必须失效。

[官方主机指南](https://unixnu.com/zh/guide/requirements)支持 Linux 源码安装，并明确 skip-setup 与 seed-data-grow 需要 macOS。最快避免全新 LLVM 冷构建的候选是干净 macOS runner：c1ecc5b 的 [macos.yml](https://github.com/UniXNU/UniXNU/blob/c1ecc5b74c9dc4ced13bcfbd81a2c3998025a5e5/.github/workflows/macos.yml)选择 macos-26 / Xcode26.6，内核使用 Xcode clang，Homebrew LLVM 不覆盖 PATH。此通用 workflow 只上传文本日志，不上传内核/镜像；若需要可下载产物，必须另设明确的设备产物步骤。它也不能代替真机验证。

Allenwdk 的 [README](https://github.com/Allenwdk/xnu-linux-toolchain)与[研究记录](https://github.com/Allenwdk/xnu-linux-toolchain/blob/main/docs/LINUX-BUILD.md)可用来理解 MIG、ld64 和 Linux host shims，但不能当这套官方工具链的无差别替代：其 portable-kalloc 补丁退化类型元数据，IIG RPC 是桩，冷构建依赖私有 firehose 头，且未做启动测试。README 的 205 秒是既有32核主机的内核构建数字，不是完整正确 LLVM 的冷构建时间。

## 用户原始 URL 覆盖表

以下均已联网核对页面，GitHub 项目还核对了 README/元数据；注明例外。只把它们当参考，没有执行其刷写、删除、密钥或发布指令。

| 原始 URL | 可达与用途/限制 |
| --- | --- |
| https://github.com/HeliumStudio-Dev/hackios-xnu-kernel | 可达；默认 raphael-bringup，Redmi K20 Pro / SM8150、XNU8796/iOS16.5、RAM上传链。不是 K70 镜像。 |
| https://github.com/oosexgang/hakios-xnu-kernel | 可达；HakiOS common、redwood/SC7280 与 QEMU，使用 XNU8796.121.2。驱动与 loader ABI 不能直接混入 UniXNU。 |
| https://github.com/oosexgang/hakios-xnu-kernel/blob/main/README.zh.md | 可达；文件自己提示中文译文过时，应以英文 README/docs 为准。 |
| https://github.com/luuu20309/ios16_on_polaris_mix2s | 可达；Polaris/SDM845 专用 shim、ADT、HFS+ 根路径，硬件地址和参数不能复用 K70。 |
| https://github.com/luuu20309/ios16_on_polaris_mix2s/releases/tag/polaris-kernel-0090 | 可达；只有 Mach-O 内核，不是 boot.img；发布方报告临时启动的触控观察。 |
| https://github.com/luuu20309/ios16_on_polaris_mix2s/blob/main/docs/COOLAPK-POLARIS-INSTALL.md | 可达（通过 raw 正文读取）；永续 flash boot 路径尚未独立验证；不能执行到 K70。 |
| https://unixnu.com/porting/ | 可达；架构与移植入口。 |
| https://unixnu.com/devices/ | 可达；设备记录索引，级别不能替代子系统实测。 |
| https://github.com/yyx2025/UniXNU-vermeer | 可达；K70 从 sheng 派生的板级参考，README 明确无 K70 硬件验证。 |
| https://github.com/mansar1337/UniXNU-vermeer | 可达；源码内核构建基线，UFS/GPU/touch关闭，不能当完整启动镜像。 |
| https://github.com/wuan0039/k70-unixnu-build | 可达；只有 .github 和 patch，没有 README；已读取 build-kernel.yml，属 macOS 上 UFS-less 链接试验。 |
| https://github.com/xyyyds0106/k70-unixnu-build-20261022 | 可达；固定源码平台内核 CI；README 主动区分其 CI、另外的 UEFI 菜单和未证实 XNU。 |
| https://unixnu.com/devices/sheng | 可达；共享 SM8550/kit 实现和分阶段证据参考；其面板、存储策略、触控与分区不属于 K70。 |
| https://github.com/matejkoo217/UniXNU-fuxi | 可达；GitHub parent 为 UniXNU/UniXNU-sheng；当前 README 和 board.name 仍是 sheng，不能按仓库名认定 fuxi 移植完成。 |
| https://github.com/hanzi123-bilp/unixnu-fuxi | 可达；board.name=fuxi、kit=sheng；README 仍含 sheng 文本，需按实际代码审查。 |
| https://github.com/hanzi123-bilp/uefi-fuxi | 可达；默认 fuxi 分支，Mu-Silicium 派生固件参考；平台地址/厂商模块不属 K70。 |
| https://github.com/Xyvern08371/unixnu-fuxi | 可达；独立 fuxi loader 探针路线，README 当前停点为 XNU _start / MMU，未进入平台驱动或系统。 |
| https://github.com/0721GPT/UniXNU-fuxi | 可达；GitHub parent=Sohytrs/UniXNU-fuxi；board=fuxi，README 仍含 sheng内容；不能复用二进制。 |
| https://github.com/zc47796741-pixel/UniXNU-gts9uwifi | 可达；board=gts9uwifi/kit=sheng，README 仍含 sheng；三星硬件配置不能移到 K70。 |
| https://unixnu.com/zh/devices/ishtar#configuration | 可达；记录很多 implemented 但无真机证据；存储/显示关闭、GPU桩。可学配置而非成功系统。 |
| https://github.com/peter8889/UniXNU-ishtar | 可达；README 明确尚未真机加载，适合最小 bringup 配置参考。 |
| https://unixnu.com/zh/xnu/boot-arguments#apply-arguments | 可达；合并、重打包、命令行校验。 |
| https://github.com/unixnu | 可达；组织入口，确认 common、SM8550和组件仓库职责。 |
| https://github.com/oosexgang/hakios-uefi | 可达；XNUP container自定义路径，当前说明相关 branding改动未构建/启动；需要本机厂商模块。 |
| https://github.com/oosexgang/hakios-metal | 可达；针对 Adreno7c+/a6xx 命令和分层测试；不能当 K70 Adreno740 的完整Metal驱动。 |
| https://hackios-web.vercel.app/en/guide | 可达；HakiOS 文档；其工作状态指 redwood/QEMU，不是 K70。 |
| https://unixnu.com/zh/porting/steps | 可达；八步流程与输入。 |
| https://unixnu.com/zh/porting/device-repository | 可达；格式2和证据记录。 |
| https://unixnu.com/zh/porting/packaging | 可达；固定脚本与 TOML 打包。 |
| https://github.com/Allenwdk/xnu-linux-toolchain | 可达；源码工具链研究，有明确已知缺口，无 releases。 |
| https://github.com/Allenwdk/xnu-linux-toolchain/blob/main/docs/LINUX-BUILD.md | 可达；历史可行性研究，部分缺口由官方后续实现解决，不应回退到降级桩。 |
| https://github.com/lzfse/lzfse | 可达；压缩算法与命令行工具，用于kernelcache/UDIF解码，不是OS/内核。 |
| https://github.com/tpoechtrager/cctools-port | 可达；当前默认1030.6.3-ld64-956.6，Linux Mach-O工具，官方固定此系列。 |
| https://github.com/markmentovai/bootstrap_cmds/tree/cross_platform | 可达；MIG跨平台分支，含构建前提；只解决工具链一环。 |
| https://github.com/ahzcpp/UniXNU-vermeer | 用户指定目标，由主代理处理干净检出与改动；本报告没有改动或提交该仓库。 |

原请求中 oosexgang/hakios-xnu-kernel 出现两次，上表合并。没有把任何参考照片、共享仓库的自述或 CI 成功升级为这台 K70 的运行结论。
