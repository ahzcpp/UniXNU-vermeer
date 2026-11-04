# iPadOS 17.7.11 双卷候选

本轮在电脑上生成了 iPad7,1 iPadOS 17.7.11（21H461）的 OS/SystemOS
双卷 SHNGROOT v1 镜像，9,382,658,048 字节。2026-11-04 10:16（UTC+8），
该镜像已通过 fastboot 写入本机 userdata，17/17 稀疏分块发送和写入均为
OKAY；配套 storage-native-02-r3 boot_b 及重启也为 OKAY。首次完整 payload
实机照片随后停在加载器交接画面：顶部绿色和底部黄色可见，没有可见的青色、
蓝色或 XNU 日志。这是 r3 的历史结果。后续只换 boot_b 的 r4 已越过交接，出现根卷挂载、
认证决策记录、SystemOS/Data 准备以及 launchd/SpringBoard 执行签名通过；
最终 watchdogd 因 SIGTRAP 退出，尚未显示桌面。详见 [r4 实机记录](boot05r4-handoff-2026-11-04.md)。

画面记录 PA=`8C007C000`、SIZE=`20000000`、ENTRY=`8C7812488`、
ARGS=`8C870C000`、LOADER=`C65B6C58`、FB=`B8000000`。黄色标记位于
拥有窗口的缓存维护之后；接下来的 MMU/缓存关闭交接需要继续定位。保留的
`NO OS ROOT` 是通用加载器里未更新的调试字符串，不能用它判断实际 payload
身份。已写入的 userdata 保持不动，后续先修正启动交接并只替换 boot。

双卷镜像本身不包含 Data 种子、根策略或 GPU addon。当前配套 boot payload
携带修正后的 packed Data 种子，storage 内核嵌入两卷的已验证认证策略。

镜像 SHA-256：
`fd43ee7035ce185ce4215ac93fdc006fd784a54b3b7442e21457c226cbe03093`。

| 窗口 | 镜像内相对偏移 | 字节数 | 原始窗口 SHA-256 |
| --- | --- | --- | --- |
| OS | 1,048,576 | 6,077,580,800 | `e5634d04a8ac35f024845f583394e0a35deb26a04cb0c00286a8674e25a9a4f2` |
| SystemOS | 6,079,643,648 | 3,303,014,400 | `b4908cde41d23e6bc05e6af51a2c9eeb00cf0f863a6a18b1600c96a3fd70414f` |

构建前重新核对两份 DMG 的完整大小和 SHA-256，使用固定 UniXNU 版本
解码且不修改上游文件；实际 decoder/util/userdata 源已与固定提交逐
字节对比。为限制峰值内存，本地构建器只将 RAW/zero/ignore 段等价
切分，未改变解码规则。一次主机 I/O 中断后，保留完整文件并补齐最终
哈希，没有将未完成中间文件标为成功。

随后以不导入构建器/核心模块的验证器独立全量回读，通过：SHNGROOT
头哈希、双卷 SHA-256、整镜像 SHA-256、对齐与零填充、OS 磁盘 GPT
主备头和条目 CRC、APFS NXSB 几何及对象校验和。SystemOS 原始 APFS
容器也通过对应检查。详见
[公开验证清单](system-candidate-verification-2026-11-04.json)。这些检查
不验证每个 APFS 文件、系统服务、根策略或真实硬件。

只使用此前测得的 userdata 容量 `242,778,615,808` 字节作边界。没有
编造 userdata 的绝对 LBA；OS 窗口内的 Apple GPT 不属于手机 GPT。
原生存储驱动仍需在本机成功读取真实分区、发布窗口并匹配根卷。

早期 clean 诊断内核关闭原生 UFS、原生 RAM block、owner Data mount 和
SystemOS union。随后已构建另一个 storage 内核，接入这些功能并完成离线
ABI、认证表和最终 payload 回读。macOS CI 制作并修正的 Data 种子保留原卷
身份，221 项活动 inode 的所有权/模式已独立检查；详见
[Data 记录](../system-next/data-seed-status-public-2026-11-04.txt)。这些检查仍
不能替代真实硬件启动。

首次完整系统写入使用的 r3 boot（随后已由 r4 替换）：135,774,208 字节，SHA-256
`db4e96a673612be1f0b48e1602dd3d46dac383aad0269cb9d9136433a104eb9c`。
它使用 FD `845d1790c4eda640435a17858cfbd4df4d91258e3fdcd904b453b78565631b9a`；
同一 FD 已由 boot05r3 基线在强制 512 MiB 窗口下运行到 XNU/BSD，约 430.75 MiB
空闲。当前完整 storage 内核 `962381af05f3eee0c41cd08708ffba34299693388220cc994598fb63728f70d5`
在 r4 中已实际通过根挂载、SystemOS/Data 准备并到达 launchd/SpringBoard exec；
服务稳定性和图形桌面仍待验证。

菜单标题保留 `K70 storage01 RAM512 authenticated UFS + packed Data - UNTESTED`，
默认 3 秒后启动；编号来自未改动的 FAT payload，不表示用了旧 FD。当前编译的
BootProgressMin 接管文字诊断显示，不能靠删除 `-v` 或 `boot-progress=2` 自动
得到苹果 LOGO。系统进度呈现还需正确连接图形控制台及相关初始化路径。

工具随 `boot04-system-source.patch` 提供；应用后见
`research/system-build/README.md`。完整 Apple 镜像保存在本地构建/交付目录，
本轮已写入获授权的手机，没有提交到 Git。
