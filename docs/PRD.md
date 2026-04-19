# efvi-cpp 项目需求文档（PRD）v1.2

**AMD Solarflare ef_vi C++ 封装库**
兼容网卡：X2522（EF10）/ X3522（EfCT）
目标平台：Linux · C++11 · OpenOnload 8.1.26
文档日期：2025-04

---

## 变更记录

| 版本 | 日期 | 变更内容 |
|------|------|---------|
| v1.0 | 2025-04 | 初始版本，5 个待确认问题 |
| v1.1 | 2025-04 | 确认 Q-01~Q-05；新增多队列需求；确定日志回调接口；文档格式转 Markdown；明确文档输出要求 |
| v1.2 | 2025-04 | 语言标准从 C++17 降至 C++11；CMake 最低版本降至 3.10；接口适配 C++11（string_view→const string&，span→裸指针+count）；设计决策更新 |

---

## 目录

1. [项目背景与目标](#1-项目背景与目标)
2. [约束条件与技术环境](#2-约束条件与技术环境)
3. [功能需求](#3-功能需求)
4. [非功能需求](#4-非功能需求)
5. [模块划分与接口设计](#5-模块划分与接口设计)
6. [测试需求](#6-测试需求)
7. [开发里程碑](#7-开发里程碑)
8. [关键设计决策](#8-关键设计决策)
9. [已确认问题清单](#9-已确认问题清单)
10. [文档输出要求](#10-文档输出要求)

---

## 1. 项目背景与目标

efvi-cpp 是对 AMD Solarflare ef_vi 内核旁路网络 API 的 C++11 封装库，面向超低延迟量化交易基础设施场景（A 股市场数据接收、行情订阅、订单通路）。

ef_vi 是一个 L2 原生 API，提供直接访问网卡数据路径的能力，完全绕过内核 TCP/IP 栈，实现亚微秒级收发包延迟。原始 C API 使用复杂、资源管理繁琐，本项目以 C++11 对其进行简洁封装，同时兼容 X2522（EF10 架构）和 X3522（EfCT 架构）两款网卡。

### 1.1 核心价值主张

- **极简初始化**：少量代码完成 VI 创建、内存注册、过滤器配置全流程
- **架构透明**：X2522/X3522 差异由库内部自动适配，用户代码无感知
- **零开销抽象**：热路径（收发包）不使用虚函数，不动态分配内存
- **可观测性**：内置原子统计计数器，支持快照导出；日志通过用户回调输出
- **多队列支持**：支持 VISet 多 VI 多队列分发（RSS），可按核绑定
- **测试友好**：Mock 模式可在无网卡环境下完整运行单元测试（CI 友好）

---

## 2. 约束条件与技术环境

| 维度 | 要求 |
|------|------|
| 操作系统 | Linux（OpenEuler 22.03 SP3，Kernel ≥ 4.15） |
| 编译器 | GCC 7+，标准 C++11（GCC 10+ 推荐） |
| 构建系统 | CMake 3.10+ |
| Onload 版本 | OpenOnload 8.1.26，`ONLOAD_SRC_DIR` 指向源码树 |
| 目标网卡 | X2522（EF10 架构）、X3522（EfCT 架构） |
| VI 设计 | 单 VI 基础设计；多队列通过 VISet 扩展 |
| 内存模型 | 热路径无动态分配；X3522 必须使用 hugepage |
| 依赖限制 | 仅依赖 OpenOnload 头文件 + 标准库，禁止引入第三方框架 |
| Mock 模式 | 无网卡环境可编译运行，用于 CI / 单元测试 |
| 测试框架 | Google Test（gtest） |
| TX 接口 | 仅支持连续 buffer，不支持 scatter-gather |
| 日志接口 | 用户提供回调函数，库不直接输出到 stderr |

---

## 3. 功能需求

### 3.1 初始化与资源管理（FR-INIT）

- **FR-INIT-01**：提供 `ViConfig` 配置结构体
- **FR-INIT-02**：`Vi` 类构造时完成所有初始化，析构时自动释放（RAII）
- **FR-INIT-03**：支持查询 NIC 架构类型（`EF10` / `EfCT`）
- **FR-INIT-04**：初始化失败抛出 `ViException`（含错误码 + 描述字符串）
- **FR-INIT-05**：提供 `NicInfo` 结构体
- **FR-INIT-06**：支持多 `Vi` 实例并发创建

### 3.2 发包（TX，FR-TX）

- **FR-TX-01**：`send(const void* buf, size_t len)` 统一接口
- **FR-TX-02**：X3522 软件 checksum（`checksum.cpp`）
- **FR-TX-03**：CTPIO 发包支持
- **FR-TX-04**：X2522 PIO 发包路径
- **FR-TX-05**：TX 完成事件自动处理
- **FR-TX-06**：`send_batch()` 批量接口（仅 X2522）

### 3.3 收包（RX，FR-RX）

- **FR-RX-01**：`poll(RxBatch& batch)` 统一接口
- **FR-RX-02**：EF10 RX ring refill 自动管理
- **FR-RX-03**：EfCT `PacketRef` RAII 对象
- **FR-RX-04**：每次 poll 处理 `EF_VI_EVENT_POLL_MIN_EVS` 个事件
- **FR-RX-05**：RX hardware timestamp 提取
- **FR-RX-06**：RX discard 事件统计

### 3.4 多队列支持（FR-MULTIQUEUE）

- **FR-MQ-01**：`ViSet` 类封装多队列
- **FR-MQ-02**：队列数量可配
- **FR-MQ-03**：每队列独立 `Vi` 实例
- **FR-MQ-04**：聚合统计接口
- **FR-MQ-05**：EF10/EfCT 差异内部屏蔽

### 3.5 过滤器管理（FR-FILT）

- **FR-FILT-01**：UDP/TCP IPv4 精确匹配
- **FR-FILT-02**：组播过滤
- **FR-FILT-03**：MAC + VLAN 过滤
- **FR-FILT-04**：`FilterCookie` 管理 add/remove
- **FR-FILT-05**：X3522 过滤器数量限制（256/Port）检查

### 3.6 性能参数调节（FR-PERF）

- **FR-PERF-01**：RXQ/TXQ 深度可配（默认 512，范围 64~4096，必须为 2 的幂）
- **FR-PERF-02**：RX refill batch size 可配（8/16/32/64）
- **FR-PERF-03**：CTPIO threshold 可配（默认 64 字节）
- **FR-PERF-04**：PD flags 可配
- **FR-PERF-05**：RX buffer 内存对齐可配
- **FR-PERF-06**：Hugepage 开关

### 3.7 统计与监控（FR-STAT）

- **FR-STAT-01**：原子计数器
- **FR-STAT-02**：`Stats::snapshot()` 无锁快照
- **FR-STAT-03**：`NicStats` 硬件统计
- **FR-STAT-04**：`reset()` 接口
- **FR-STAT-05**：`latency_ns` 字段

### 3.8 日志接口（FR-LOG）

- **FR-LOG-01**：`LogCallback` 字段
- **FR-LOG-02**：`LogLevel` 枚举（DEBUG/INFO/WARN/ERROR）
- **FR-LOG-03**：未设置回调时完全静默
- **FR-LOG-04**：热路径不产生日志调用
- **FR-LOG-05**：初始化/过滤器/错误路径可产生日志

---

## 4. 非功能需求

| 编号 | 类别 | 要求 |
|------|------|------|
| NFR-01 | 延迟 | 热路径不调用 malloc/free，不使用虚函数 |
| NFR-02 | 吞吐 | 单核 ≥ 5M pps 收包（64B UDP） |
| NFR-03 | 内存安全 | RAII 完整释放所有资源 |
| NFR-04 | 异常安全 | 初始化异常不产生资源泄漏 |
| NFR-05 | 可移植性 | 头文件路径通过 CMake 配置 |
| NFR-06 | 编译隔离 | Mock 模式不 include ef_vi 头文件 |
| NFR-07 | 测试覆盖 | 核心模块 ≥ 85% |
| NFR-08 | 文档 | 所有 public API 提供 Doxygen 注释 |
| NFR-09 | 文档随代码 | 每次 commit 同步更新 docs/ |

---

## 5. 模块划分与接口设计

见代码库 `include/efvi/` 目录。

---

## 6. 测试需求

总计目标：≥ 83 个单元测试，覆盖率 ≥ 85%（核心模块）

---

## 7. 开发里程碑

| 阶段 | 内容 | 状态 |
|------|------|------|
| Phase 4-A | 项目骨架、CMake、Mock 模式、基础测试框架 | 完成 |
| Phase 4-B | Vi RAII 初始化、ViConfig、NicInfo、ViException、NicArch、LogCallback | 完成 |
| Phase 4-C | EF10 TX 路径 | 进行中 |
| Phase 4-D | EF10 RX 路径 | 待开始 |
| Phase 4-E | EfCT TX | 待开始 |
| Phase 4-F | EfCT RX | 待开始 |
| Phase 4-G | Filter 管理 | 待开始 |
| Phase 4-H | ViSet 多队列 | 待开始 |
| Phase 4-I | Stats + examples + README + Doxygen | 待开始 |
| Phase 5-A | 实机集成测试 | 待开始 |

---

## 8. 关键设计决策

| 决策 | 决定 | 理由 |
|------|------|------|
| 热路径多态 | 编译期 if/switch on arch()，不用虚函数 | 避免 vptr 间接调用 |
| 内存分配 | 预分配 + 热路径零动态分配 | malloc 引入 jitter |
| 统计计数器 | `std::atomic`（`memory_order_relaxed`） | 最低开销 |
| X3522 checksum | 软件实现（`checksum.cpp`） | EfCT 不支持 TX checksum offload |
| 错误处理 | 初始化路径用 exception，热路径用返回值 | 初始化非热路径 |
| 接口隔离（pimpl） | `Vi::Impl` 隐藏所有 ef_vi C 头文件 | 隔离 C API 污染 |
| Mock 模式 | 编译宏 `EFVI_MOCK_MODE` 完全隔离 | CI 环境无法安装 OpenOnload |
| TX 接口 | 仅支持连续 buffer | 简化接口 |
| 日志接口 | `LogCallback` 用户回调，库静默 | 避免日志库依赖 |
| TX Alternatives | 不封装 | 超出当前场景 |

---

## 9. 已确认问题清单

| 编号 | 问题 | 决定 |
|------|------|------|
| Q-01 | TX Alternatives 是否封装？ | 不封装 |
| Q-02 | RSS 多队列是否需要？ | 需要，新增 `ViSet` 类 |
| Q-03 | X3522 `rx_future_peek` 是否封装？ | 不封装 |
| Q-04 | TX 是否支持 scatter-gather？ | 不支持 |
| Q-05 | 诊断日志输出方式？ | 用户回调（`LogCallback`） |

---

## 10. 文档输出要求

- Phase 4-B 提交：`docs/PRD.md`（v1.2）
- Phase 4-I 提交：`docs/USAGE.md`
- Phase 5-A：`docs/` 全部文档与实现对齐

---

*efvi-cpp PRD v1.2 | 所有设计决策已确认 | 当前阶段：Phase 4-B*
