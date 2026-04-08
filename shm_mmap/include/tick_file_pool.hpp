#pragma once
/**
 * @file tick_file_pool.hpp
 * @brief 逐笔文件预分配池
 *
 * 启动时批量 ftruncate 创建若干 pool_XXXX.mmap 文件，
 * 运行时各通道按需 rename 取用（pool → 正式路径），消除运行时 IO 抖动。
 */
#include <memory>
#include <mutex>
#include <queue>
#include <string>

class MmapFile;

class TickFilePool {
public:
    /**
     * @param pool_dir       预分配文件存放目录（须提前创建）
     * @param slot_size      sizeof(TickRecord)
     * @param slots_per_file 每个文件的最大 slot 数
     * @param total_capacity 全量预分配容量（条数），
     *                       num_files = ceil(total_capacity / slots_per_file)
     */
    TickFilePool(const std::string& pool_dir,
                 size_t             slot_size,
                 uint64_t           slots_per_file,
                 uint64_t           total_capacity);

    /**
     * @brief 批量预分配（阻塞，启动阶段调用）
     *
     * 对每个 pool_XXXX.mmap：
     *   1. open + ftruncate(kHeaderSize + slots_per_file × slot_size)
     *   2. mmap 写入基础 FileHeader（magic/version/capacity/data_offset）
     *   3. munmap + close
     *   4. 路径推入 free_files_ 队列
     *
     * 已存在的文件跳过创建（支持重启复用）。
     * @throws std::system_error  若文件创建或 ftruncate 失败
     */
    void preallocate();

    /**
     * @brief 从池中取出一个文件并 rename 到 formal_path
     *
     * 线程安全（mutex 保护队列）。rename 原子替换，对读端透明。
     * 取出后调用方须更新 FileHeader 中的 channel/file_seq/seq_start 等字段。
     *
     * @param formal_path  目标正式路径（父目录须已存在）
     * @param out          成功时写入 MmapFile unique_ptr（读写模式）
     * @return 池未耗尽且 rename 成功返回 true，否则 false
     */
    bool acquire(const std::string&         formal_path,
                 std::unique_ptr<MmapFile>* out);

    /// 池中剩余可用文件数
    size_t remaining() const noexcept;

    /// 每个文件的 slot 容量
    uint64_t slots_per_file() const noexcept { return slots_per_file_; }

private:
    std::string  pool_dir_;
    size_t       slot_size_;
    uint64_t     slots_per_file_;
    uint32_t     num_files_;

    std::queue<std::string> free_files_;
    mutable std::mutex      mu_;
};
