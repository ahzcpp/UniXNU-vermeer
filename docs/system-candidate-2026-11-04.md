# iPadOS 17.7.11 双卷候选

本轮在电脑上生成了 iPad7,1 iPadOS 17.7.11（21H461）的 OS/SystemOS
双卷 SHNGROOT v1 镜像，9,382,658,048 字节。没有写入手机；它不包含
Data 种子、根策略或 GPU addon，不是已验证可刷入并进入桌面的系统。

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

当前 clean kernel 关闭原生 UFS、原生 RAM block、owner Data mount 和
SystemOS union。Linux/WSL 上本轮也没有创建 APFS Data 种子。因此这份
双卷封装不能替代 XNU 入口、存储、Data、系统卷整合和驱动适配。

工具随 `boot04-system-source.patch` 提供；应用后见
`research/system-build/README.md`。完整 Apple 数据仅保存在本地 WSL，
没有提交到 Git。
