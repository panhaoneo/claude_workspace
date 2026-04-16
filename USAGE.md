# 延时统计分析工具 —— 使用说明

## 目录

1. [环境依赖](#1-环境依赖)
2. [快速开始](#2-快速开始)
3. [命令行参数详解](#3-命令行参数详解)
4. [使用场景示例](#4-使用场景示例)
5. [输出报告说明](#5-输出报告说明)
6. [内存控制](#6-内存控制)
7. [运行原理简述](#7-运行原理简述)
8. [常见问题](#8-常见问题)

---

## 1. 环境依赖

| 依赖 | 版本要求 | 说明 |
|------|---------|------|
| Python | ≥ 3.9 | 标准库即可运行 |
| numpy | 推荐安装 | 分位数计算更快；无则自动回退纯 Python |
| sort | 系统内置 | GNU coreutils `sort`，外排使用 |

```bash
# 安装 numpy（可选但推荐）
pip3 install numpy
```

---

## 2. 快速开始

### 2.1 数据文件准备

将 TAP 时间戳中间文件放入同一目录，文件命名须符合规范：

```
/data/
├── md_sh_10.0.1.1_10.0.1.2_20241226_tap_tmp.txt
├── md_sh_10.0.1.3_10.0.1.4_20241226_tap_tmp.txt
├── tbt_sh_10.0.1.1_10.0.1.2_20241226_tap_tmp.txt
└── tbt_sh_10.0.1.3_10.0.1.4_20241226_tap_tmp.txt
```

### 2.2 运行工具

```bash
python3 latency_stat.py \
  --data-dir /data \
  --date 20241226 \
  --exchange sh \
  --baseline "10.0.1.1_10.0.1.2" \
  --output report_20241226.md
```

运行完毕后，`report_20241226.md` 即为完整分析报告，可直接粘贴至文档系统。

---

## 3. 命令行参数详解

```
usage: latency_stat.py [-h]
                       --data-dir DATA_DIR
                       --date DATE
                       --exchange {sh,sz,all}
                       --baseline BASELINE
                       [--mode {md,tbt,all}]
                       [--output OUTPUT]
                       [--reservoir]
                       [--reservoir-size N]
                       [--tmp-dir TMP_DIR]
                       [--outlier-ns NS]
```

### 必填参数

| 参数 | 格式 | 说明 |
|------|------|------|
| `--data-dir` | 目录路径 | 存放 `*_tap_tmp.txt` 文件的目录 |
| `--date` | `YYYYMMDD` | 分析日期，仅处理文件名中含该日期的文件 |
| `--exchange` | `sh` / `sz` / `all` | 交易所过滤；`all` 表示同时处理沪深 |
| `--baseline` | `src_ip_dst_ip` | 基准路标识，与文件名中格式一致，例如 `10.0.1.1_10.0.1.2` |

### 可选参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--mode` | `all` | `md`：仅快照；`tbt`：仅逐笔；`all`：全部 |
| `--output` | stdout | 输出文件路径；不指定则打印到终端 |
| `--reservoir` | 关闭 | 启用 Reservoir Sampling，限制内存占用 |
| `--reservoir-size` | `1000000` | Reservoir 容量（条数），仅 `--reservoir` 生效时有效 |
| `--tmp-dir` | `/tmp` | 外排临时文件存放目录，需有读写权限 |
| `--outlier-ns` | `0`（不过滤） | 过滤延时绝对值超过 N ns 的异常记录，0 表示不过滤 |

---

## 4. 使用场景示例

### 场景 A：仅分析快照，输出到终端

```bash
python3 latency_stat.py \
  --data-dir /data \
  --date 20241226 \
  --exchange sh \
  --baseline "10.0.1.1_10.0.1.2" \
  --mode md
```

### 场景 B：同时分析沪深两市，结果写入文件

```bash
python3 latency_stat.py \
  --data-dir /data \
  --date 20241226 \
  --exchange all \
  --baseline "10.0.1.1_10.0.1.2" \
  --output report_all_20241226.md
```

### 场景 C：逐笔大文件，启用 Reservoir Sampling 控制内存

```bash
python3 latency_stat.py \
  --data-dir /data \
  --date 20241226 \
  --exchange sh \
  --baseline "10.0.1.1_10.0.1.2" \
  --mode tbt \
  --reservoir \
  --reservoir-size 2000000 \
  --output report_tbt_20241226.md
```

> Reservoir Sampling 下 p99 误差 <1%，适用于内存资源紧张的场景。

### 场景 D：过滤异常延时（>10s 的脏数据）

```bash
python3 latency_stat.py \
  --data-dir /data \
  --date 20241226 \
  --exchange sh \
  --baseline "10.0.1.1_10.0.1.2" \
  --outlier-ns 10000000000 \
  --output report_20241226.md
```

### 场景 E：外排临时文件指定 SSD 目录（加速排序）

```bash
python3 latency_stat.py \
  --data-dir /data \
  --date 20241226 \
  --exchange sh \
  --baseline "10.0.1.1_10.0.1.2" \
  --tmp-dir /ssd/tmp \
  --output report_20241226.md
```

---

## 5. 输出报告说明

报告由四个 Section 组成，均为 Markdown 格式，数字列右对齐。

### Section 1：快照多路对比

- 以 `(ticker, exchange_ts)` 为 key 对齐各路数据
- 基准路一行显示 `—`，其余路显示与基准路的差值统计
- 差值为正表示该路**慢于**基准路，为负表示**快于**基准路

```markdown
## 快照对比（按交易所时间对齐，基准路：10.0.1.1_10.0.1.2）

| 路次                      |  cnt | min(ns) | max(ns) | avg(ns) | stddev | ...
|:--------------------------|-----:|--------:|--------:|--------:|-------:|...
| 10.0.1.1_10.0.1.2（基准） |    — |       — |       — |       — |      — |...
| 10.0.1.3_10.0.1.4         |  160 |     -20 |     683 |      96 |     45 |...
```

### Section 2：逐笔多路对比

- 以 `(channel, seq)` 为 key 对齐各路数据
- 每个非基准路单独一个子表，按 channel 分行展示
- 同样以差值 ns 为单位，可为负

### Section 3：快照单路绝对延时

- 每路独立统计，不 join
- 延时 = TAP 时间戳 − 交易所时间，单位 ns
- 反映该链路全天的网络 + 处理延时分布

### Section 4：逐笔单路绝对延时

- 每路一个子表，按 channel 分行
- 适合对比不同通道的延时差异

---

## 6. 内存控制

### 默认模式（无 --reservoir）

| 数据类型 | 内存占用估算 |
|----------|------------|
| 快照单路全天 | ~24 MB |
| 逐笔单路单 channel | ~40 MB |
| 逐笔多路对比（外排后 join） | diff 列表按路/channel 分组，峰值 <200 MB |

逐笔单路模式按 channel 逐个处理，处理完即释放，**不会将全文件读入内存**。

### Reservoir Sampling 模式（--reservoir）

- 每个统计组维护一个固定大小的随机样本（默认 100 万条）
- 内存上限 = 路数 × channel 数 × reservoir_size × 8B
- 统计结果中 `cnt` 仍为真实流经的数据条数，p 值有约 <1% 误差

---

## 7. 运行原理简述

```
文件扫描
  └─ file_scanner.py  按 exchange/date/mode 过滤，返回 RouteFile 列表

单路延时（Section 3/4）
  └─ latency_stat.py  流式读取每路文件
       └─ parser.py   解析行 → (tap_ts_ps, exchange_ts_ps)
                       latency_ns = (tap_ts - exch_ps) // 1000
       └─ stats.py    按 channel 分组累积，calc_stats() 计算统计量
       └─ reporter.py 格式化 Markdown 表格

多路对比（Section 1/2）
  └─ joiner.py        外排 + 多路归并 join
       └─ sorter.py   各路 → 临时文件 → sort 命令排序 → 有序迭代器
       └─ heapq.merge 对齐相同 key，取各路最小 tap_ts，计算 diff_ns
  └─ stats.py         按 channel/route 分组统计
  └─ reporter.py      格式化 Markdown 表格
```

---

## 8. 常见问题

**Q：提示"No matching files found"？**

检查以下几点：
- `--data-dir` 路径是否正确
- `--date` 是否与文件名中的日期一致（格式 YYYYMMDD）
- `--exchange` 是否与文件名中的交易所代码一致（sh/sz）
- 文件名是否严格符合 `{type}_{exchange}_{src_ip}_{dst_ip}_{date}_tap_tmp.txt` 格式

---

**Q：只有一路数据时 Section 1/2 显示"仅一路数据，跳过对比"？**

对比分析至少需要两路数据（含基准路）。单路情况下 Section 3/4 仍正常输出单路延时。

---

**Q：基准路找不到时怎么处理？**

工具会在 stderr 输出警告，对应类型（md/tbt）的对比 Section 跳过，单路 Section 不受影响。

---

**Q：外排临时文件磁盘占用多少？**

约等于参与对比的所有路文件总大小（临时文件在分析完成后自动删除）。若 `/tmp` 空间不足，请通过 `--tmp-dir` 指定有足够空间的目录。

---

**Q：`exchange_ts` 字段不是 17 位，或含非数字字符？**

该行会被丢弃并计入 parse_error，在 stderr 输出警告（`[WARN] xxx: N parse error(s) skipped`），不影响其余行的统计。

---

**Q：如何只看逐笔某一路的 channel 分布，不做多路对比？**

使用 `--mode tbt` 配合单路数据目录（只放一路文件），或配合 `--exchange` 过滤。Section 4 会按 channel 逐行展示该路的绝对延时分布。

---

**Q：差值为负数意味着什么？**

```
diff_ns = (route_tap_ts - baseline_tap_ts) / 1000
```

负值表示该路的 TAP 时间戳**早于**基准路，即该路**更快**收到了相同的行情数据。
