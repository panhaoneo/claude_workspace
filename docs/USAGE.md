# efvi-cpp 使用文档

**AMD Solarflare ef_vi C++ 封装库 — 快速上手与 API 参考**
版本：Phase 4-B | 兼容：X2522（EF10）/ X3522（EfCT）

---

## 目录

1. [环境要求](#1-环境要求)
2. [编译与安装](#2-编译与安装)
3. [快速开始](#3-快速开始)
4. [配置参数参考](#4-配置参数参考)
5. [多队列使用](#5-多队列使用)
6. [性能调优指南](#6-性能调优指南)
7. [API 速查表](#7-api-速查表)
8. [X2522 vs X3522 差异说明](#8-x2522-vs-x3522-差异说明)
9. [常见错误与解决](#9-常见错误与解决)

---

## 1. 环境要求

| 项目 | 要求 |
|------|------|
| 操作系统 | Linux（内核 ≥ 4.15，OpenEuler 22.03 SP3 验证通过） |
| 编译器 | GCC 7+，C++11 标准（推荐 GCC 10+） |
| 构建系统 | CMake 3.10+ |
| OpenOnload | 8.1.26（真实网卡模式需要） |
| 测试框架 | Google Test（系统包或自动下载） |
| 网卡 | AMD Solarflare X2522（EF10）或 X3522（EfCT） |

Mock 模式（`-DEFVI_MOCK_MODE=ON`）不需要 OpenOnload 和网卡，适用于 CI 环境。

---

## 2. 编译与安装

### 2.1 Mock 模式（CI / 单元测试，无需网卡）

```bash
mkdir build && cd build
cmake .. -DEFVI_MOCK_MODE=ON -DEFVI_BUILD_TESTS=ON
cmake --build . -j$(nproc)
ctest --output-on-failure
```

### 2.2 真实网卡模式

```bash
mkdir build && cd build
cmake .. \
  -DONLOAD_SRC_DIR=/path/to/openonload \
  -DEFVI_BUILD_TESTS=ON \
  -DEFVI_BUILD_EXAMPLES=ON
cmake --build . -j$(nproc)
```

### 2.3 作为子模块集成

```cmake
add_subdirectory(efvi-cpp)
target_link_libraries(your_target PRIVATE efvi-cpp)
```

---

## 3. 快速开始

以下示例在 30 行内展示完整的收包流程：

```cpp
#include "efvi/vi.hpp"
#include "efvi/config.hpp"
#include <cstdio>

int main() {
    // 1. 配置
    efvi::ViConfig cfg;
    cfg.interface = "eth0";
    cfg.log_callback = [](efvi::LogLevel lvl, const std::string& msg) {
        fprintf(stderr, "[%d] %s\n", static_cast<int>(lvl), msg.c_str());
    };

    // 2. 添加 UDP 过滤器（接收发往 224.0.0.1:5000 的组播）
    cfg.filters.push_back(efvi::FilterSpec::multicast_ip(0xe0000001));

    // 3. 初始化（RAII：失败抛 ViException，成功则资源由析构自动释放）
    efvi::Vi vi(cfg);
    fprintf(stderr, "arch=%s\n",
            vi.arch() == efvi::NicArch::EF10 ? "EF10/X2522" : "EfCT/X3522");

    // 4. 收包循环
    efvi::RxBatch batch;
    while (true) {
        int n = vi.poll(batch);
        for (int i = 0; i < n; ++i) {
            const void* data = batch[i].data();
            size_t len       = batch[i].len();
            uint64_t ts_ns   = batch[i].timestamp_ns();
            (void)data; (void)len; (void)ts_ns;
            // 处理数据包...
        }
    }

    // 5. 统计输出
    auto s = vi.stats_snapshot();
    fprintf(stderr, "rx=%llu drops=%llu\n",
            (unsigned long long)s.rx_packets,
            (unsigned long long)s.rx_drops);
    return 0;
}
```

---

## 4. 配置参数参考

### ViConfig 字段

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `interface` | `std::string` | — | 网卡接口名，必填（如 `"eth0"`） |
| `perf` | `PerformanceParams` | 见下表 | 性能参数 |
| `filters` | `vector<FilterSpec>` | 空 | 初始化时自动安装的过滤器列表 |
| `log_callback` | `LogCallback` | `nullptr` | 日志回调，不设置则完全静默 |

### PerformanceParams 字段

| 字段 | 类型 | 默认值 | 范围/说明 |
|------|------|--------|----------|
| `rxq_depth` | `int` | 512 | RX 队列深度，必须为 2 的幂，范围 [64, 4096] |
| `txq_depth` | `int` | 512 | TX 队列深度，必须为 2 的幂，范围 [64, 4096] |
| `rx_refill_batch` | `int` | 32 | RX ring refill 批量大小，可选 8/16/32/64 |
| `ctpio_threshold` | `int` | 64 | CTPIO 阈值（字节），范围 [0, 1500] |
| `rx_buf_align` | `int` | 4MB | RX buffer 对齐（字节） |
| `use_hugepages` | `bool` | false | X3522 时自动强制为 true |
| `pd_flags` | `unsigned` | 0 | PD 标志：`EF_PD_DEFAULT`/`EF_PD_VF`/`EF_PD_PHYS_MODE`（X2522 only） |

### LogLevel 枚举

```cpp
enum class LogLevel { DEBUG, INFO, WARN, ERROR };
```

### FilterSpec 工厂方法

```cpp
FilterSpec::udp(uint32_t ip, uint16_t port)         // UDP local IP:Port
FilterSpec::tcp(uint32_t ip, uint16_t port)         // TCP local IP:Port
FilterSpec::multicast_all()                          // 所有组播
FilterSpec::multicast_ip(uint32_t mcast_ip)          // 特定组播 IP
FilterSpec::mac_vlan(const uint8_t mac[6], int vlan) // MAC + VLAN
```

---

## 5. 多队列使用

### 5.1 创建 ViSet

```cpp
efvi::ViSetConfig vscfg;
vscfg.queue_count    = 4;
vscfg.base_cfg.interface = "eth0";

efvi::ViSet vs(vscfg);
```

### 5.2 每队列绑核

```cpp
#include <pthread.h>

void pin_thread(int cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
}

// 每队列在独立线程上 poll
std::vector<std::thread> threads;
for (int q = 0; q < vs.queue_count(); ++q) {
    threads.emplace_back([&vs, q]() {
        pin_thread(q);          // 绑定到 CPU core q
        efvi::RxBatch batch;
        while (running) {
            vs.vi(q).poll(batch);
            // 处理 batch...
        }
    });
}
```

### 5.3 聚合统计

```cpp
auto agg = vs.aggregate_stats();
printf("total rx=%llu drops=%llu\n",
       (unsigned long long)agg.rx_packets,
       (unsigned long long)agg.rx_drops);
```

---

## 6. 性能调优指南

### 6.1 CTPIO threshold

CTPIO 适合短帧（< 256B）。对大帧（如 1400B UDP）可适当增大：

```cpp
cfg.perf.ctpio_threshold = 256;  // 大帧时推高
```

### 6.2 RXQ 深度

深队列减少丢包但占用更多内存：

```cpp
cfg.perf.rxq_depth = 1024;   // 高负载场景
cfg.perf.rxq_depth = 128;    // 低延迟优先（减少 refill 频率）
```

### 6.3 Hugepage（X3522 必须）

```bash
# 系统级配置
echo 512 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
mount -t hugetlbfs nodev /mnt/huge
```

X3522 时库自动开启 hugepage，无需手动设置 `use_hugepages`。

### 6.4 CPU 亲和性

- 建议将 poll 线程固定到专用核（隔离核更佳）
- X3522 的 EfCT 超缓冲（superbuf）机制已在驱动层优化，无需手动干预

### 6.5 RX refill batch

较大 batch 减少系统调用次数，但会短暂降低 RX ring 饱和度：

```cpp
cfg.perf.rx_refill_batch = 64;  // 吞吐优先
cfg.perf.rx_refill_batch = 8;   // 延迟优先
```

---

## 7. API 速查表

### Vi 类

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `Vi(const ViConfig& cfg)` | — | 构造，初始化失败抛 `ViException` |
| `send(buf, len)` | `void` | 发送一帧（连续 buffer，无 scatter-gather） |
| `send_batch(bufs, lens, count)` | `void` | 批量发送（X2522 only） |
| `poll(RxBatch& batch)` | `int` | 收包，返回包数 |
| `add_filter(FilterSpec)` | `FilterCookie` | 添加过滤器，失败抛 `ViException` |
| `remove_filter(FilterCookie)` | `void` | 删除过滤器 |
| `arch()` | `NicArch` | 返回 `EF10` 或 `EFCT` |
| `nic_info()` | `const NicInfo&` | 网卡元信息 |
| `stats_snapshot()` | `Stats::Snapshot` | 无锁统计快照 |
| `reset_stats()` | `void` | 清零统计计数器 |

### ViSet 类

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `ViSet(const ViSetConfig& cfg)` | — | 创建多队列封装 |
| `vi(int queue_idx)` | `Vi&` | 获取指定队列的 Vi 实例 |
| `queue_count()` | `int` | 队列总数 |
| `aggregate_stats()` | `Stats::Snapshot` | 所有队列统计聚合 |
| `reset_all_stats()` | `void` | 清零所有队列统计 |

### PacketRef 类（RxBatch 中）

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `data()` | `const void*` | 包数据指针 |
| `len()` | `size_t` | 包长度（字节） |
| `timestamp_ns()` | `uint64_t` | 硬件 RX 时间戳（纳秒） |

---

## 8. X2522 vs X3522 差异说明

| 行为 | X2522（EF10） | X3522（EfCT） |
|------|-------------|-------------|
| TX 发包路径 | CTPIO 优先，fallback DMA/PIO | 仅 CTPIO + store-and-forward |
| TX Checksum | 硬件 offload | **软件计算**（`checksum.cpp`） |
| `send_batch()` | 支持（减少 doorbell） | 同 `send()` 多次调用 |
| RX buffer | 用户管理，库自动 refill | 驱动 superbuf，自动管理 |
| `PacketRef` 释放 | 析构时无操作 | 析构时调用 `efct_vi_rxpkt_release` |
| Hugepage | 可选 | **强制开启** |
| `EF_PD_PHYS_MODE` | 支持 | **抛 ViException**（不支持） |
| 过滤器上限 | 无软件限制 | **256 个/port**（超出抛 ViException） |
| 多队列机制 | `ef_vi_set` VISet | EfCT shared RX queue |

**关键注意事项：**

1. **X3522 软件 checksum**：TX 帧中 IP/TCP/UDP checksum 必须由调用者预先正确填写（或使用库提供的 `efvi::detail::ip_checksum()` 等辅助函数）。
2. **X3522 PacketRef 生命周期**：`RxBatch` 中的 `PacketRef` 必须在下次 `poll()` 之前处理完毕，离开作用域后自动释放（RAII）。
3. **EF10 RX buffer**：离开 `RxBatch` 作用域后，`PacketRef::data()` 指向的内存可能被 refill 覆盖，不要持久保存指针。

---

## 9. 常见错误与解决

### ViException 常见错误码

| 错误码 | 场景 | 解决方法 |
|--------|------|---------|
| `EINVAL` (22) | 参数非法（空接口名、非 2 幂 rxq_depth、X3522+PHYS_MODE 等） | 检查 `ViConfig` 字段 |
| `ENODEV` (19) | 驱动打开失败 | 确认 OpenOnload 已安装，`modprobe sfc` 已加载 |
| `ENOMEM` (12) | PD/VI/内存注册分配失败 | 检查系统内存，X3522 需配置 hugepage |
| `ENOSPC` (28) | X3522 过滤器数量超限 256 | 减少过滤器或合并规则 |
| `EBUSY` (16) | 过滤器添加失败（规则冲突或资源占用） | 检查是否有重复过滤器 |
| `ENOENT` (2) | 删除不存在的 FilterCookie | 确保使用 `add_filter()` 返回的 cookie |
| `ERANGE` (34) | `ViSet::vi(idx)` 越界 | 检查 `queue_idx` < `queue_count()` |

### 典型问题

**Q: `ef_driver_open failed` — 错误码 19**
```
A: OpenOnload 未安装或驱动未加载。
   sudo modprobe sfc
   sudo /usr/bin/openonload-install
```

**Q: X3522 初始化成功但 RX 无数据**
```
A: EfCT 使用 RX_REF 事件，确认 poll() 处理了 PacketRef 并及时释放。
   检查 vi.stats_snapshot().rx_drops 是否增加。
```

**Q: 高负载下 rx_drops 增加（EF10）**
```
A: RX ring 耗尽。尝试：
   - 增大 rxq_depth（512→1024）
   - 增大 rx_refill_batch（32→64）
   - 在 poll 循环中减少每包处理时间
```

**Q: EfCT TX 发包后对端 checksum 校验失败**
```
A: X3522 不支持 TX checksum offload，需软件填写：
   #include "src/checksum.hpp"
   ip_hdr->check = efvi::detail::ip_checksum(ip_hdr, ip_hdr_len);
   udp_hdr->check = efvi::detail::udp_checksum(src_ip, dst_ip, udp_hdr, udp_len);
```

---

*efvi-cpp USAGE.md | Phase 4-B | 更多示例见 `examples/` 目录*
