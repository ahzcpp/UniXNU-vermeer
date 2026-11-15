# K70 不可写 NVRAM 代理后端：407 离线候选

这是 Redmi K70 / vermeer 的一项实验源码，目标是补齐 NVRAM 控制器的注册路径，检验它是否造成 Wi-Fi 设置页面等待数分钟。**407/408 只完成离线构建和检查，尚未在手机上测试；不能据此宣称 Wi-Fi 页面、Safari 或 App Store 已修复。**

环境是现代 XNU 10063.141.1 加 iPhone X 的 20H392 用户态及预链接组件。工作目录中的 `ipados-17.7.11-21H461` 是构建树名称；本次分析的 AppleImage4 实际来自 20H392，版本为 AppleImage4-202.120.2。

## 问题与实现

静态分析发现：AppleImage4 启动时等待 `IONVRAM` 资源，现有 K70 只有 `/chosen` 中的 CHRP 代理数据，没有具体控制器。系统根节点可能因此保持 busy。它与 Wi-Fi 页面卡住的因果关系仍需运行日志和界面测量确认。

本目录有三项必须配套的改动：

| 部分 | 内容 | 原因 |
| --- | --- | --- |
| 399 控制器 | 校验、复制并读取现有代理 bank；写和擦除返回 NotWritable | 提供真实的读取后端，不能伪造持久化成功 |
| 401 v2 核心补丁 | 对实际绑定的只读 provider 拒绝缓存修改和同步 | 原 XNU 的 setter 只接受到内存，失败同步仍可能清除 dirty；控制器单独拒绝写入不够 |
| 403 平台补丁 | 在 `dtNVRAM` 赋值后同步挂接，并保留真实 `/options` 对象 | 消除异步匹配竞争，避免 `/options` 被普通空 nub 接管 |

控制器在正式基类注册前调用真实 IODTNVRAM 预绑定，并要求**本实例实际完成 32 字节 bank 头读取**。已有隐藏控制器时，不覆盖它、不发布资源。预绑定前就保留永久引用；之后失败也保留引用，因为 CHRP 持有裸指针且没有解绑契约。

资源只通过 `IONVRAMController::registerService` 的标准路径发布。`read/select` 只访问私有深拷贝，不回调 IODTNVRAM；这些调用发生在 NVRAM 锁内。

## 启用条件与语义

默认不创建控制器。显式启用需要 `/chosen/k70-nvram-proxy-ro` 为 **OSData，长度 4，字节 `01 00 00 00`**。缺失、0、错误类型或长度都不启用。没有新增启动参数，也没有提高启动参数长度上限。

要求原代理数据完整、CHRP 校验通过、单 bank、current bank 为 0。当前实际输入是 8192 字节的 version 1 bank。没有代理数据时拒绝注册，不合成替代 bank。

`readonly=true`、`persistent=false`；`sync(void)` 的空实现不能表示持久化完成。401 补丁使 IODTNVRAM 同步和 CHRP 序列化明确返回 NotWritable，包括限流、不可锁及重复调用。写入保留原有权限、类型、大小检查；只读拒绝发生在修改缓存之前。旧 DELETE 别名只对实际只读 provider 传播 NotWritable，其他历史行为保留。

AppleImage4 的具体 `nonce-seeds` OSData 内核调用路径已在编译后的 ARM64 指令中复核：新分支返回 `0xe00002cf`，由原 AppleImage4 映射为 errno 83，记录错误后继续启动。这不代表其他原有错误均不会触发 panic。

## 文件与复现

- `src/`：控制器、便携校验核心、C 导出声明。
- `tests/`、`run_tests399.py`：直接使用生产实现的主机夹具测试；IOKit 和容器为模拟对象。
- `patches/`：IONVRAM.cpp、IONVRAMCHRPHandler.cpp、IOPlatformExpert.cpp 的完整差异。原 Apple 开源代码及修改继续受其 APSL 2.0 条款约束；本仓库 LICENSE 适用于新增自有代码。
- `workstation/`：原工作站的构建、集成和审计脚本快照。包含本地绝对路径及既有流水线依赖，迁移机器前需要映射路径和提供相应环境；它们不是独立的一键安装器。
- `SOURCE-MANIFEST.json`：源码快照的文件摘要。

在有 clang++ 和 Python 3 的 Linux 主机中执行：

```sh
cd drivers/k70-proxy-nvram-ro
python3 run_tests399.py
```

该测试使用 ASan/UBSan，不需要手机或固件。原工作站结果为 30 次运行、65304 项断言。它不验证真实 IOKit 锁、启动调度或硬件。

原生构建使用实际 XNU 的 `IONVRAMController.cpp` 编译配方，更换输入/输出并启用 `IOKIT_ENABLE_SHARED_PTR`。保留普通内建 C++ 初始化，**不能套用依赖预链接 Apple 父类的 late initializer 处理**。把控制器归档与两份补丁后的 cpo 一起链接；核对构造函数位于 `__DATA_CONST,__mod_init_func` 且先于 `last_kernel_constructor`。

原工作站 407 相对 394 的直接对象仅替换 IONVRAM.cpo、IOPlatformExpert.cpo，另增加一个控制器归档。其余 1018 个直接对象保留。NECP 377 v4、RAM 375、观察器 380，以及原 GPU、无线固件与 owner-root 信任输入继续使用。共同外部归档引用保持，但其中未在父版清单固定摘要的文件缺少历史摘要证明，不能把引用相同当成所有历史字节均已核验。

## 已有证据与下一次实测

401 的 175 个函数只有预定 4 个变化，导出接口及常量数据一致；403 的 181 个函数只有预定 2 个变化。399 的 201 个导入均在现有原生内核中定义，两个 getter 分别为 6 和 13 条指令，只做原子读取、比较和返回。独立 404 审查补充了 9286 项主机断言；405 复核了实际返回值指令。

407/408 使用同一个内核、同一组用户态资源，NECP/RAM 选项都为 1，仅改变 NVRAM selector 为 1/0，及重新生成的 owner nonce/签名摘要。实际 FAT、签名绑定、Data 种子、固件与 995 字节启动参数已回读核对。

后续需测量实际控制器绑定、AppleImage4 注册、root busy/quiet 时间、Wi-Fi 页面进入耗时，再用真实 Safari 和 App Store 验证联网。早于启用时已获准的在途缓存写入不会被追溯取消；此次改动只用于受控的早期启动挂接。它也不提供 NVRAM 持久保存功能。

本目录不包含手机固件、Apple/OEM 二进制、bank 原始内容、密钥或网络配置。
