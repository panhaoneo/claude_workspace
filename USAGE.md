# x3522-tune 使用说明

适用工具：`x3522-tune`、`compare_latency.sh`、`monitor_irq.sh`
参考规格：AMD UG1523 / XN-201257-CD，OpenOnload 8.1.26+，Linux Kernel 5.15+

---

## 目录

1. [环境要求](#1-环境要求)
2. [安装](#2-安装)
3. [快速上手](#3-快速上手)
4. [x3522-tune 详细用法](#4-x3522-tune-详细用法)
   - [check — 检测模式](#41-check--检测模式)
   - [apply — 自动修复模式](#42-apply--自动修复模式)
   - [report — 对比报告模式](#43-report--对比报告模式)
5. [完整参数说明](#5-完整参数说明)
6. [各检测项说明](#6-各检测项说明)
7. [compare_latency.sh — 延迟对比报告](#7-compare_latencysh--延迟对比报告)
8. [monitor_irq.sh — 实时中断监控](#8-monitor_irqsh--实时中断监控)
9. [典型调优工作流](#9-典型调优工作流)
10. [CI 集成示例](#10-ci-集成示例)
11. [常见问题](#11-常见问题)

---

## 1. 环境要求

| 依赖 | 最低版本 | 用途 |
|------|----------|------|
| Linux Kernel | 5.15 | EfCt 驱动支持 |
| OpenOnload | 8.1.26 | ef_vi API |
| bash | 4.2 | 脚本运行时 |
| python3 | 3.6 | `report` 子命令 |
| ethtool | 5.4 | M4 中断聚合检测 |
| cpupower | 任意 | M6 CPU governor 修复（`cpupowerutils` 包）|
| numactl | 任意 | M5 NUMA 拓扑（建议安装）|

> **OpenEuler 22.03 提示：** 安装依赖：
> ```bash
> dnf install -y ethtool cpupowerutils numactl python3
> ```

---

## 2. 安装

```bash
# 克隆或下载后，进入目录
chmod +x x3522-tune compare_latency.sh monitor_irq.sh

# 可选：放到 PATH 中全局使用
sudo cp x3522-tune /usr/local/bin/
sudo cp compare_latency.sh monitor_irq.sh /usr/local/bin/
```

`check` 模式读取 `/proc/irq/*/smp_affinity` 需要 root 权限；`apply` 模式必须以 root 运行。

---

## 3. 快速上手

```bash
# 1. 以 root 检测当前配置（接口 eth0，应用线程绑在 CPU 2 和 3）
sudo x3522-tune check --iface eth0 --app-cpus 2,3

# 2. 查看问题后，用 dry-run 预览修复命令
sudo x3522-tune apply --iface eth0 --app-cpus 2,3 --dry-run

# 3. 确认无误后执行修复
sudo x3522-tune apply --iface eth0 --app-cpus 2,3

# 4. 再次检测，确认全部通过
sudo x3522-tune check --iface eth0 --app-cpus 2,3
```

期望的最终输出：

```
汇总: 0 FAIL, 0 WARN, 7 PASS
```

---

## 4. x3522-tune 详细用法

### 4.1 check — 检测模式

读取当前系统配置，逐项与建议值对比，输出 PASS / WARN / FAIL。

```
x3522-tune check --iface <接口> --app-cpus <CPU列表> [选项]
```

**示例：**

```bash
# 基础检测
sudo x3522-tune check --iface eth0 --app-cpus 2,3

# 指定较大 ring size 和多队列
sudo x3522-tune check --iface eth0 --app-cpus 2,3 --ring-size 8192 --queues 4

# 输出 JSON，保存为基线快照（用于后续对比）
sudo x3522-tune check --iface eth0 --app-cpus 2,3 --json > baseline.json
```

**退出码：**

| 退出码 | 含义 |
|--------|------|
| `0` | 所有检测项均为 PASS |
| `1` | 存在至少一个 FAIL（WARN 不影响退出码） |

---

### 4.2 apply — 自动修复模式

执行所有 FAIL 和 WARN 项的修复命令，必须以 root 运行。

```
x3522-tune apply --iface <接口> --app-cpus <CPU列表> [选项]
```

**建议流程：先 dry-run，再 apply。**

```bash
# 第一步：预览将要执行的命令（不实际修改系统）
sudo x3522-tune apply --iface eth0 --app-cpus 2,3 --dry-run

# 第二步：确认命令合理后执行
sudo x3522-tune apply --iface eth0 --app-cpus 2,3
```

**持久化说明：**

- hugepages 修复会同时写入 `/etc/sysctl.d/99-x3522.conf`，重启后生效。
- IRQ affinity 修复写入 `/proc/irq/N/smp_affinity`，重启后失效，建议写入启动脚本。
- CPU governor 修复通过 `cpupower` 当场生效，重启后失效，建议写入 `/etc/rc.local` 或 `tuned`。

---

### 4.3 report — 对比报告模式

比较两个 `--json` 快照，输出每项状态变化及改善/退步统计。

```
x3522-tune report --before <before.json> --after <after.json>
```

**示例：**

```bash
# 调优前采集基线
sudo x3522-tune check --iface eth0 --app-cpus 2,3 --json > before.json

# … 执行调优操作 …

# 调优后采集快照
sudo x3522-tune check --iface eth0 --app-cpus 2,3 --json > after.json

# 生成对比报告
x3522-tune report --before before.json --after after.json
```

**输出示例：**

```
=================================================================
  X3522-TUNE 对比报告
  Before : before.json  (2025-08-01T10:00:00+08:00)
  After  : after.json   (2025-08-01T10:30:00+08:00)
=================================================================
  [M1] hugepages          ❌ FAIL ✨→ ✅ PASS
       Before: Free hugepages=0 < 需求=5
       After : nr_hugepages=64, 需求=5, Free=59
  [M2] irq_affinity       ❌ FAIL ✨→ ✅ PASS
  [M3] irqbalance         ❌ FAIL ✨→ ✅ PASS
  [M6] cpu_governor       ⚠️  WARN ✨→ ✅ PASS
=================================================================
  改善项: 4  退步项: 0
=================================================================
```

`✨→` 表示改善，`⚡→` 表示退步。

---

## 5. 完整参数说明

```
选项                   默认值   说明
--iface <ifname>       (必填)   NIC 接口名，如 eth0、enp1s0f0
--app-cpus <list>      (必填)   应用线程 CPU 列表
                                支持格式: "2,3"  "2-5"  "2,4-6,8"
--ring-size <n>        4096     ef_vi RX ring size（用于计算 hugepages 需求）
                                X3 支持最大 6144，必须是 512 的倍数
--queues <n>           1        使用的 RX 队列数（影响 hugepages 需求计算）
--rx-usecs <n>         50       期望的 interrupt moderation 值（µs），apply 时写入
--json                 off      以 JSON 格式输出（供 CI / 监控集成）
--apply                off      自动执行修复命令（需 root）
--dry-run              off      打印修复命令但不实际执行
-h, --help                      显示帮助
```

---

## 6. 各检测项说明

### M1 — hugepages（🔴 必须）

**检测内容：** `/proc/meminfo` 中 `HugePages_Free` 是否满足 ef_vi VI 建立所需。

**需求量公式：**
```
需求 hugepages = (ring_size / 1024 + 1) × queues
```

| ring_size | queues | 需求 |
|-----------|--------|------|
| 4096 | 1 | 5 页 |
| 4096 | 4 | 20 页 |
| 6144 | 9 | 63 页 |

**修复命令（apply 时自动执行）：**
```bash
echo 64 > /proc/sys/vm/nr_hugepages
echo 'vm.nr_hugepages = 64' >> /etc/sysctl.d/99-x3522.conf
```

---

### M2 — irq_affinity（🔴 必须）

**检测内容：** 网卡的 RX MSI IRQ 是否绑到了应用核上（不应绑）。

**原因：** X3 与 X2 不同，所有 16 个 RX 队列只要有流量就触发中断，包括非本应用流量。若中断打到应用核会打断轮询循环、污染 cache。

**判断逻辑：**
- RX IRQ 的 `smp_affinity` mask 与 `--app-cpus` 的 mask **无交集** → PASS
- TX IRQ（最后一个 MSI IRQ）绑应用核为正常（cache 局部性）

**修复策略：** 将 RX IRQ 绑到同 NUMA 节点、但非应用核的 CPU 上。

---

### M3 — irqbalance（🔴 必须）

**检测内容：** `irqbalance` 服务是否正在运行。

**原因：** irqbalance 会动态迁移 IRQ，破坏手动设置的亲和性，可能将中断调度到应用核。

**修复命令：**
```bash
systemctl disable --now irqbalance
```

---

### M4 — interrupt_moderation（🟡 重要）

**检测内容：** `ethtool -c <iface>` 读取的 `rx-usecs` 是否在合理范围。

| rx-usecs | 状态 | 说明 |
|----------|------|------|
| 25–100 | PASS | 推荐范围 |
| 0 | WARN | 中断聚合关闭，CPU 负载较高 |
| > 100 | WARN | 可能影响内核路径延迟 |

> 注意：ef_vi 轮询路径**不依赖中断**，此项只影响内核旁路以外的路径和 CPU 负载，不影响 ef_vi 本身的延迟。

---

### M5 — numa_topology（🟢 推荐）

**检测内容：** 应用核是否全部在 NIC 所在 NUMA 节点。

**原因：** 跨 NUMA 内存访问延迟比同 NUMA 高 30–100ns，影响 DMA 和共享内存路径。

**修复：** 本项只给出建议，需用户重新规划线程绑定。

---

### M6 — cpu_governor（🟢 推荐）

**检测内容：** 应用核的 `/sys/.../scaling_governor` 是否为 `performance`。

**原因：** `powersave` 等节能模式会进入 C-state 休眠，唤醒延迟可达数十 µs，直接影响轮询循环响应时间。

**修复命令：**
```bash
cpupower -c 2,3 frequency-set -g performance
```

---

### M7 — ring_size_advisor（🟢 推荐）

**检测内容：** 当前 hugepages 是否满足指定 `--ring-size` 和 `--queues` 的需求，并打印完整需求对照表。

**与 M1 的区别：** M1 是硬性通过/失败检测；M7 提供参考表格，辅助选择合理的 ring_size。

---

## 7. compare_latency.sh — 延迟对比报告

汇总多个阶段的 `eflatency` 输出文件，生成 P50/P99/P99.9/Max 对比表格及相对基线的变化百分比。

**使用方法：**

```bash
# 1. 在各阶段运行 eflatency 并保存结果（文件名格式：<阶段>_latency.txt）
eflatency -i eth0 -n 100000 2>&1 | tee baseline_latency.txt
# … 执行 IRQ 调优 …
eflatency -i eth0 -n 100000 2>&1 | tee after_irq_tune_latency.txt
# … 执行 CPU governor 调优 …
eflatency -i eth0 -n 100000 2>&1 | tee after_governor_latency.txt

# 2. 生成对比报告（默认在当前目录查找 *_latency.txt）
./compare_latency.sh

# 3. 也可以指定目录
./compare_latency.sh /path/to/results/
```

**预定义阶段名（按此顺序显示）：**

| 文件名 | 含义 |
|--------|------|
| `baseline_latency.txt` | 优化前基线 |
| `after_irq_tune_latency.txt` | IRQ 亲和性调优后 |
| `after_governor_latency.txt` | CPU Governor 调优后 |
| `after_full_tune_latency.txt` | 完整调优后 |

其他名称的 `*_latency.txt` 文件会自动追加到末尾。

**可选：同目录放置以下文件获得额外信息：**

```bash
cat /proc/interrupts > irq_before.txt   # 调优前 IRQ 快照
# … 调优 …
cat /proc/interrupts > irq_after.txt    # 调优后 IRQ 快照
# compare_latency.sh 会自动 diff 并展示 sfc/efct 相关行
```

---

## 8. monitor_irq.sh — 实时中断监控

每秒打印一次各网卡 IRQ 在每个应用核上的中断计数，用于验证 IRQ affinity 设置是否生效。

```
monitor_irq.sh --iface <接口> --app-cpus <CPU列表> [--interval <秒>] [--count <次数>]
```

**示例：**

```bash
# 持续监控（Ctrl-C 停止），每秒刷新
sudo ./monitor_irq.sh --iface eth0 --app-cpus 2,3

# 采样 30 次后自动退出
sudo ./monitor_irq.sh --iface eth0 --app-cpus 2,3 --count 30

# 每 2 秒采样一次
sudo ./monitor_irq.sh --iface eth0 --app-cpus 2,3 --interval 2
```

**输出解读：**

```
IRQ      CPU2/s     CPU3/s     Total/s      描述
────────────────────────────────────────────────────────────
34       0          0          1250         sfc-0
35       0          0          0            sfc-1
```

- `CPU2/s` / `CPU3/s`：该 IRQ 每秒打到应用核的次数
- 若任一应用核列不为 0，该行高亮为**红色**并显示 `⚠️ 应用核收到中断!`
- 目标：应用核列全为 0

---

## 9. 典型调优工作流

```
Phase 0：采集基线
    ├── x3522-tune check ... --json > baseline_tune.json
    └── eflatency ... > baseline_latency.txt

Phase 1：修复必须项（🔴）
    ├── x3522-tune apply ... --dry-run          # 预览
    ├── x3522-tune apply ...                    # 执行
    └── eflatency ... > after_irq_tune_latency.txt

Phase 2：修复推荐项（🟢）
    ├── cpupower frequency-set -g performance
    └── eflatency ... > after_governor_latency.txt

Phase 3：验证
    ├── x3522-tune check ... --json > after_tune.json
    ├── x3522-tune report --before baseline_tune.json --after after_tune.json
    ├── compare_latency.sh                      # 延迟对比
    └── monitor_irq.sh ... --count 30           # 确认 IRQ 不打应用核
```

---

## 10. CI 集成示例

```bash
#!/bin/bash
# ci_check.sh - 在 CI 中验证 X3522 配置是否符合要求

set -euo pipefail

IFACE="${X3522_IFACE:-eth0}"
APP_CPUS="${X3522_APP_CPUS:-2,3}"

# 运行检测，JSON 输出
RESULT=$(sudo x3522-tune check \
    --iface "$IFACE" \
    --app-cpus "$APP_CPUS" \
    --json 2>&1)

echo "$RESULT"

# 检查是否有 FAIL
FAIL_COUNT=$(echo "$RESULT" | python3 -c "
import json, sys
d = json.load(sys.stdin)
print(d['summary']['fail'])
")

if [[ "$FAIL_COUNT" -gt 0 ]]; then
    echo "CI FAIL: x3522-tune 发现 ${FAIL_COUNT} 个 FAIL 项" >&2
    exit 1
fi

echo "CI PASS: x3522-tune 所有检测项通过"
```

---

## 11. 常见问题

**Q: check 模式报 `无法读取 /sys/class/net/eth0/device/msi_irqs`**

A: 请确认：
1. 接口名正确（`ip link` 查看实际名称，可能是 `enp1s0f0` 等）
2. sfc 驱动已加载：`lsmod | grep sfc`
3. 以 root 运行：`sudo x3522-tune check ...`

---

**Q: M2 irq_affinity 建议的 mask 全为 0**

A: 工具找不到同 NUMA 节点的非应用核（所有非应用核在另一 NUMA）。建议手动指定：
```bash
# 手动将 RX IRQ 绑到 CPU4（mask=0x10）
echo 10 > /proc/irq/34/smp_affinity
```

---

**Q: apply 后重启系统，设置失效了**

A: 持久化方式：

| 项目 | 持久化方法 |
|------|-----------|
| hugepages | apply 已写 `/etc/sysctl.d/99-x3522.conf`，重启自动生效 |
| irqbalance | `systemctl disable irqbalance` 已持久化 |
| IRQ affinity | 写入 `/etc/rc.d/rc.local` 或使用 `tuned` 自定义 profile |
| CPU governor | 使用 `tuned-adm profile latency-performance` 或写入 rc.local |

---

**Q: M6 报 `unknown(no cpufreq)`**

A: 内核未加载 cpufreq 驱动，或 CPU 已被 `intel_pstate` / `amd-pstate` 接管。检查：
```bash
ls /sys/devices/system/cpu/cpu0/cpufreq/ 2>/dev/null || echo "no cpufreq"
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_driver 2>/dev/null
```
若使用 `intel_pstate`：
```bash
echo performance > /sys/devices/system/cpu/cpu2/cpufreq/energy_performance_preference
```

---

**Q: eflatency 在哪里？**

A: 位于 OpenOnload 源码目录 `src/tests/ef_vi/eflatency.c`，需先编译：
```bash
cd /path/to/openonload
make -C src/tests/ef_vi eflatency
```
若未能编译，可用 `tcpdump` 配合外部打时间戳替代，但精度为 µs 级（vs eflatency 的 ns 级）。
