WCN7850 重连发送元数据修复：本地验证，尚待 K70 实机验收

上游来源：Linux fdea4d44e4b9c3f7021c85f8cd766e84e224472d
https://github.com/torvalds/linux/commit/fdea4d44e4b9c3f7021c85f8cd766e84e224472d

问题：ath12k_dp_vdev_tx_attach() 使用 |= 累积 tcl_metadata。同一 link slot 换用 vdev 后，旧的 vdev/pdev 位可能残留。修复使用 =，每次从当前 vdev/pdev 重新计算；后续清除 VALID_HTT 的原逻辑保持原样。认证、密钥、授权、owner/generation 和 firmware 均不改变。

本地基线：Ath12kWifi 使用的 Linux 源码 pin 为 458c6079fc1d41d564c37679c8ace02cd83ee817。请先检查当前源码是否仍存在相同的 |= 写法；如果已是 =，无需重复应用。本补丁的 a/dp.c 路径对应已经准备好的 ath12k 源码根目录。先备份并执行 patch --dry-run -p1 < upstream-tcl-metadata.patch，确认只有这一处运算符变化后再应用。不要把其他机型的二进制驱动直接覆盖到手机。

本地验证：用原有 ARM64 XNU 编译配方重编未修改 control，与实际基线 ath12k_dp.o 字节一致；修改版只改变 ath12k_dp_vdev_tx_attach 的本地指令，无新增导入/导出。128 成员 archive 相对 diagnostic431 仅 ath12k_dp.o 改变，其余127成员一致。新内核完成链接、预链接、存储绑定和107个构造函数顺序检查；实际启动包的 FAT、FD、ADT 和本地 owner 签名也已核对。未操作分区或合并主分支。

主机回归：旧完整函数在 0 -> 1 -> 0 重用序列产生旧位残留；修改后的完整函数通过16392组序列、link、vdev、pdev、旧值及原bank错误路径检查。附带 test_attach_metadata.c 是带桩的实际函数回归，可运行：
clang -std=c11 -O1 -g -fsanitize=address,undefined test_attach_metadata.c -o test_attach_metadata
./test_attach_metadata

边界：主机桩不模拟真实布局、DMA、射频或固件。本机原先出现过握手成功后 DHCP/数据失败，与上游报告的握手超时不完全相同；因此这个真实源码缺陷不能直接视作本机全部故障的根因。仍需实测自动连接、开关后重连、DHCP、DNS、正常TLS与真实Safari/App Store内容。设备当前断连，修复包尚未实机启动。它也不修复 WebKit 的独立 GPU 初始化/花屏问题。

本目录仅含公开驱动源码补丁、主机夹具和验证摘要。不含 Apple/OEM 固件、系统镜像、密钥、设备序列号或网络配置。
