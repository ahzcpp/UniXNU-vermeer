# boot05r4：交接代码写回与 MMU 分段标记

baseline r3 已在真机通过强制、无回退的 512 MiB 窗口进入 XNU/BSD。首次完整 storage-r3 则停在顶部绿色、底部黄色，未见青色或入口蓝条。其画面数值为 PA `8C007C000`、SIZE `20000000`、ENTRY `8C7812488`、ARGS `8C870C000`、LOADER `C65B6C58`、FB `B8000000`，目前不能据此认定完整内核或 UFS 驱动已经执行。

实际 MachO 布局与这些地址一致，full Used=`87FC000`，未覆盖低地址的 loader 或 framebuffer；缓存维护仍遍历完整 512 MiB，和成功的 baseline 相同。源码审查找到交接的一处缺口：此前只清理 XNU owned window，然后关 SCTLR 的 M/C/I；DarwinEnter 自身指令和 PE relocation 写入位于这个窗口之外，没有明确清理到 PoC。

r4 在关闭 MMU 前对 DarwinEnter 的全部指令 span 做真实 identity 检查并逐 VA `dc cvac` 清理到 PoC，随后 DSB、I-cache invalidation、DSB/ISB。C 端检查完整代码页并给出明确失败原因；ExitBootServices 后的汇编再次通过 AT/PAR 核对每页，拒绝改变后的非 identity 映射。汇编 span 由实际 DarwinEnter/DarwinEnterEnd 符号界定，当前编译结果为 568 字节，源码要求 64 字节起始对齐。整个路径保留 x0/x1 和已保存的 framebuffer x14/x15，不用栈、literal pool、Boot Services 或全 set/way 清缓存。

[Linux arm64 正式启动文档](https://docs.kernel.org/arch/arm64/booting.html) 的代码 clean-to-PoC、优先 VA maintenance 要求用于解释这个窄修复；没有套用 Linux 的传参 ABI，XNU 原 bootargs/entry 传参保持不变。后续四张真机照片已确认 r4 越过原交接停顿并执行完整 storage 内核。

新增色条用于分开观察阶段，编号是 framebuffer 的 1/16 区域：

| 色条 | 阶段 |
| --- | --- |
| 黄 13 | owned window 的 D-cache 清理完成 |
| 橙 10 | ExitBootServices 后代码 identity 不成立，保持 MMU 开启并停住 |
| 白 12 | 代码 identity、全部 self-code PoC 清理和 I-cache 同步完成 |
| 红 11 | 即将写 SCTLR_EL1 |
| 品红 15 | 已越过 SCTLR 写入和 ISB 后的指令取用 |
| 青 14 | 后续 TLBI、DSB、ISB 完成 |
| 顶部蓝 | 内核入口探针，不代表 UFS 或根卷已工作 |

C 端界面已改为中性的 `K70 RAM512 - XNU PAYLOAD`，显示实际 USED、RD、POL 大小，适用于同一 FD 搭载完整系统载荷。没有修改 RAM 发布策略、静态占位 HOB、内核、FAT payload 或 userdata。

模块编译/链接、serial GenFds 与完整 FD 审计通过。FD SHA-256 为 `2eb9d57346a7184b240d836c9cdfc25072c8f6eda80a5cc9256f3be40860732a`；不可变匹配审计 SHA 为 `c758cca322d99ca03b9a5236a4dd4da72589654bd7748236612b4a2202f712d5`。

使用既有 FAT payload `f02a97df…` 和完整 kernel `962381af…` 组合新的 boot-only 候选，完整独立解包回读报告附在 `r4/storage-verification.json`。镜像为 135,774,208 字节，SHA-256 `b7b24c58eadfed82450066389700ecb7806fbcc813b412fbc0a6304084a1a1a5`。它已刷入 boot_b 并重启，未重复写 userdata。随后真机出现根卷挂载、认证决策记录、SystemOS/Data mount returned 和 temporary watchdog launch policy ready，继续出现 PID 1 launchd、PID 110 liveactivitiesd、PID 127 SpringBoard 的 exec process signature accepted。

此源码增量依次应用在原 boot05/r1、r2、r3 源码补丁之后，保留所有历史版本和二进制。r4 冻结源码、报告及布局证据附在 `r4/`。补丁没有 Apple/OEM 二进制、照片、密钥或手机私有标识，也不修改基线/CI。构建继续依赖已有固定本地工作区及受检完成模块：

```sh
python3.11 /mnt/e/K70-UniXNU-20261104/boot-chain-05/incremental-ui.py
python3.11 /mnt/e/K70-UniXNU-20261104/boot-chain-05/audit-fd.py
```

使用匹配 FD 和不可变审计通过已有 storage packager v2 组合相同载荷；这个修订只需要新 boot 镜像。

## 本轮真机终点

最后画面为 `watchdogd[134] exited -- exit reason namespace 2 subcode 0x5 description: none`。固定 XNU 的 `reason.h` 与 `signal.h` 对应 `OS_REASON_SIGNAL` / `SIGTRAP`；不能写成根签名失败、watchdog 超时或已证实的 OOM。累计 BRK 从 4、12 到 27，打印 PC 为零的观察器仍需核对，不能当作真实断点地址。最后 `pages=0x1716a`、`free=0x25e`，4 KiB 页折合约 369.41 MiB 管理页与 2.37 MiB 空闲页，说明该快照内存紧张，但不证明 SIGTRAP 的唯一原因。

这些照片证明已推进到用户空间服务执行；尚无桌面显示或稳定用户空间的验收。下一轮保留已写入的 userdata 和 r4 交接修复，验证更大受检 RAM 窗口，并核对服务异常记录。公开逐项证据见 [r4 runtime](storage-native-03-r4-runtime.json)。
