#include "tick_file_pool.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "file_header.hpp"
#include "mmap_file.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// 内部工具
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// 格式化 4 位十进制数（如 7 → "0007"）
void fmt_04u(char* buf, size_t bufsz, uint32_t n) {
    ::snprintf(buf, bufsz, "%04u", n);
}

/// 创建父目录（mkdir -p）
bool mkdir_p_simple(const std::string& path) {
    // 已在 mmap_file.cpp 实现相同逻辑，此处独立实现避免循环依赖
    size_t pos = 1;
    while ((pos = path.find('/', pos)) != std::string::npos) {
        std::string sub = path.substr(0, pos);
        if (::mkdir(sub.c_str(), 0755) != 0 && errno != EEXIST) return false;
        ++pos;
    }
    return ::mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

/// 判断文件是否已存在
bool file_exists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

/**
 * @brief 创建并初始化一个 pool 文件（写完即关闭，不保持 mmap）
 *
 * 文件布局：kHeaderSize 字节头 + capacity × slot_size 字节数据区。
 * ftruncate 填零，只需显式设置非零的 FileHeader 字段。
 */
bool create_pool_file(const std::string& path,
                      size_t slot_size, uint64_t capacity)
{
    size_t file_size = kHeaderSize + capacity * slot_size;

    int fd = ::open(path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) return false;

    if (::ftruncate(fd, static_cast<off_t>(file_size)) != 0) {
        ::close(fd);
        return false;
    }

    // 仅 mmap 头部，写入 FileHeader，其余页面按需 page fault 懒分配
    void* base = ::mmap(nullptr, kHeaderSize,
                        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        ::close(fd);
        return false;
    }

    // 内存已由 ftruncate 清零，设置非零字段
    FileHeader* hdr = static_cast<FileHeader*>(base);
    hdr->magic       = kMagicTick;
    hdr->version     = kVersion;
    hdr->slot_size   = static_cast<uint32_t>(slot_size);
    hdr->capacity    = capacity;
    hdr->data_type   = 1;       // Tick
    hdr->created_at  = now_ns();
    hdr->data_offset = kHeaderSize;
    // write_index / gap_count / recovered_count 保持 0

    ::msync(base, kHeaderSize, MS_SYNC);
    ::munmap(base, kHeaderSize);
    ::close(fd);
    return true;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// TickFilePool
// ─────────────────────────────────────────────────────────────────────────────

TickFilePool::TickFilePool(const std::string& pool_dir,
                           size_t             slot_size,
                           uint64_t           slots_per_file,
                           uint64_t           total_capacity)
    : pool_dir_(pool_dir),
      slot_size_(slot_size),
      slots_per_file_(slots_per_file),
      num_files_(static_cast<uint32_t>(
          (total_capacity + slots_per_file - 1) / slots_per_file))
{}

void TickFilePool::preallocate() {
    mkdir_p_simple(pool_dir_);

    for (uint32_t i = 0; i < num_files_; ++i) {
        char idx_buf[8];
        fmt_04u(idx_buf, sizeof(idx_buf), i);
        std::string path = pool_dir_ + "/pool_" + idx_buf + ".mmap";

        // 已存在则直接复用（支持重启）
        if (!file_exists(path)) {
            if (!create_pool_file(path, slot_size_, slots_per_file_)) {
                throw std::system_error(
                    errno, std::generic_category(),
                    "TickFilePool: failed to create pool file: " + path);
            }
        }

        std::lock_guard<std::mutex> lock(mu_);
        free_files_.push(path);
    }
}

bool TickFilePool::acquire(const std::string&         formal_path,
                            std::unique_ptr<MmapFile>* out)
{
    std::string pool_path;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (free_files_.empty()) return false;
        pool_path = free_files_.front();
        free_files_.pop();
    }

    // 原子 rename：对读端透明（只有在正式路径可见后读端才能打开）
    if (::rename(pool_path.c_str(), formal_path.c_str()) != 0) {
        // rename 失败：归还到队列
        std::lock_guard<std::mutex> lock(mu_);
        free_files_.push(pool_path);
        return false;
    }

    // 以读写模式打开正式文件，供调用方更新 FileHeader
    *out = MmapFile::open_readwrite(formal_path);
    if (!*out) {
        // 打开失败，文件已 rename 但无法访问
        return false;
    }

    return true;
}

size_t TickFilePool::remaining() const noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    return free_files_.size();
}
