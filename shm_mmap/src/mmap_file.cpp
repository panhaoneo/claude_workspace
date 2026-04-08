#include "mmap_file.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// ─────────────────────────────────────────────────────────────────────────────
// 内部工具
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// 递归创建目录（等价于 mkdir -p）
bool mkdir_p(const std::string& path) {
    size_t pos = 1;  // 跳过首个 '/'
    while ((pos = path.find('/', pos)) != std::string::npos) {
        std::string sub = path.substr(0, pos);
        if (::mkdir(sub.c_str(), 0755) != 0 && errno != EEXIST) return false;
        ++pos;
    }
    return ::mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

/// 创建 file_path 的父目录
bool make_parent_dir(const std::string& file_path) {
    size_t pos = file_path.rfind('/');
    if (pos == std::string::npos) return true;
    return mkdir_p(file_path.substr(0, pos));
}

/// 打开并 ftruncate 一个新文件，失败时关闭并返回 -1
int create_and_truncate(const std::string& path, size_t file_size) {
    int fd = ::open(path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) return -1;
    if (::ftruncate(fd, static_cast<off_t>(file_size)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

/// mmap 整个文件（fd 已 fstat 得到 file_size）
void* mmap_file(int fd, size_t file_size, bool readonly) {
    int prot  = readonly ? PROT_READ : (PROT_READ | PROT_WRITE);
    void* ptr = ::mmap(nullptr, file_size, prot, MAP_SHARED, fd, 0);
    return (ptr == MAP_FAILED) ? nullptr : ptr;
}

/// 通过 fstat 获取文件大小
size_t file_size_of(int fd) {
    struct stat st;
    if (::fstat(fd, &st) != 0) return 0;
    return static_cast<size_t>(st.st_size);
}

/// 初始化 FileHeader 公共字段（内存须已清零）
void init_common_header(FileHeader* hdr,
                         uint64_t magic, size_t slot_size,
                         uint64_t capacity, uint8_t exchange, uint8_t data_type,
                         uint64_t data_offset) {
    hdr->magic       = magic;
    hdr->version     = kVersion;
    hdr->slot_size   = static_cast<uint32_t>(slot_size);
    hdr->capacity    = capacity;
    hdr->exchange    = exchange;
    hdr->data_type   = data_type;
    hdr->created_at  = now_ns();
    hdr->data_offset = data_offset;
    // write_index / gap_count / recovered_count 已由 memset 置 0
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// 构造 / 析构
// ─────────────────────────────────────────────────────────────────────────────

MmapFile::MmapFile(int fd, void* base, size_t mapped_size,
                   size_t slot_size, bool readonly) noexcept
    : fd_(fd), base_(base), mapped_size_(mapped_size),
      slot_size_(slot_size), readonly_(readonly) {}

MmapFile::~MmapFile() {
    if (base_ && base_ != MAP_FAILED) {
        ::munmap(base_, mapped_size_);
    }
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 工厂：写端
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<MmapFile> MmapFile::create_snapshot(
    const std::string& path,
    uint64_t           capacity,
    const char*        symbol,
    uint8_t            exchange)
{
    const size_t slot_size   = sizeof(SnapshotRecord);
    const size_t index_size  = capacity * sizeof(IndexEntry);
    const size_t data_size   = capacity * slot_size;
    const size_t file_size   = kHeaderSize + index_size + data_size;

    make_parent_dir(path);

    int fd = create_and_truncate(path, file_size);
    if (fd < 0) return nullptr;

    void* base = mmap_file(fd, file_size, false);
    if (!base) { ::close(fd); return nullptr; }

    // ftruncate 已将内存清零，直接设置非零字段
    FileHeader* hdr = static_cast<FileHeader*>(base);
    init_common_header(hdr, kMagicSnapshot, slot_size, capacity,
                       exchange, 0 /*Snapshot*/, kHeaderSize + index_size);
    hdr->index_offset = kHeaderSize;
    if (symbol) {
        ::strncpy(hdr->symbol, symbol, sizeof(hdr->symbol) - 1);
        hdr->symbol[sizeof(hdr->symbol) - 1] = '\0';
    }

    return std::unique_ptr<MmapFile>(
        new MmapFile(fd, base, file_size, slot_size, false));
}

std::unique_ptr<MmapFile> MmapFile::create_tick(
    const std::string& path,
    uint64_t           capacity,
    uint32_t           channel,
    uint16_t           file_seq,
    uint8_t            exchange,
    uint64_t           seq_start)
{
    const size_t slot_size = sizeof(TickRecord);
    const size_t file_size = kHeaderSize + capacity * slot_size;

    make_parent_dir(path);

    int fd = create_and_truncate(path, file_size);
    if (fd < 0) return nullptr;

    void* base = mmap_file(fd, file_size, false);
    if (!base) { ::close(fd); return nullptr; }

    FileHeader* hdr = static_cast<FileHeader*>(base);
    init_common_header(hdr, kMagicTick, slot_size, capacity,
                       exchange, 1 /*Tick*/, kHeaderSize);
    hdr->channel   = channel;
    hdr->file_seq  = file_seq;
    hdr->seq_start = seq_start;
    hdr->seq_end   = 0;

    return std::unique_ptr<MmapFile>(
        new MmapFile(fd, base, file_size, slot_size, false));
}

// ─────────────────────────────────────────────────────────────────────────────
// 工厂：读端 / pool acquire
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<MmapFile> MmapFile::open_readonly(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return nullptr;

    size_t fsz = file_size_of(fd);
    if (fsz < kHeaderSize) { ::close(fd); return nullptr; }

    void* base = mmap_file(fd, fsz, true);
    if (!base) { ::close(fd); return nullptr; }

    size_t slot_size = static_cast<const FileHeader*>(base)->slot_size;
    return std::unique_ptr<MmapFile>(
        new MmapFile(fd, base, fsz, slot_size, true));
}

std::unique_ptr<MmapFile> MmapFile::open_readwrite(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) return nullptr;

    size_t fsz = file_size_of(fd);
    if (fsz < kHeaderSize) { ::close(fd); return nullptr; }

    void* base = mmap_file(fd, fsz, false);
    if (!base) { ::close(fd); return nullptr; }

    size_t slot_size = static_cast<const FileHeader*>(base)->slot_size;
    return std::unique_ptr<MmapFile>(
        new MmapFile(fd, base, fsz, slot_size, false));
}

// ─────────────────────────────────────────────────────────────────────────────
// 写端接口
// ─────────────────────────────────────────────────────────────────────────────

int64_t MmapFile::append_snapshot(const SnapshotRecord& rec) {
    FileHeader* hdr = header();

    // 声明下一个 slot（单调递增，单写线程）
    uint64_t idx = hdr->write_index.fetch_add(1, std::memory_order_acq_rel);
    if (idx >= hdr->capacity) {
        // 不回退 write_index：文件已满，后续调用同样返回 -1
        return -1;
    }

    // ① 先写数据 region
    char* data_ptr = static_cast<char*>(base_)
                   + hdr->data_offset
                   + idx * slot_size_;
    ::memcpy(data_ptr, &rec, slot_size_);

    // ② 后写索引 valid（release store），确保读端看到 valid=1 时数据已就绪
    char* idx_ptr = static_cast<char*>(base_)
                  + hdr->index_offset
                  + idx * sizeof(IndexEntry);
    IndexEntry* entry = reinterpret_cast<IndexEntry*>(idx_ptr);
    entry->data_time  = rec.data.data_time;
    entry->slot_index = static_cast<uint32_t>(idx);
    entry->valid.store(1, std::memory_order_release);

    hdr->last_written_at = now_ns();
    return static_cast<int64_t>(idx);
}

bool MmapFile::write_tick(uint64_t slot_index, const TickRecord& rec) {
    FileHeader* hdr = header();
    if (slot_index >= hdr->capacity) return false;

    char* slot_ptr = static_cast<char*>(base_)
                   + hdr->data_offset
                   + slot_index * slot_size_;
    TickRecord* slot = reinterpret_cast<TickRecord*>(slot_ptr);

    // ① 先写数据字段（除 state）
    slot->record_type = rec.record_type;
    ::memcpy(&slot->data, &rec.data, sizeof(rec.data));

    // ② 后写 state（release store），保证读端在 acquire load 后能看到完整数据
    slot->state.store(static_cast<uint8_t>(SlotState::kValid),
                      std::memory_order_release);

    // write_index 仅用于监控统计，relaxed 即可
    hdr->write_index.fetch_add(1, std::memory_order_relaxed);
    hdr->last_written_at = now_ns();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 通用读端接口
// ─────────────────────────────────────────────────────────────────────────────

const void* MmapFile::at(uint64_t index) const noexcept {
    const FileHeader* hdr = header();
    if (index >= hdr->capacity) return nullptr;
    return static_cast<const char*>(base_)
         + hdr->data_offset
         + index * slot_size_;
}

void* MmapFile::at_mutable(uint64_t index) noexcept {
    if (readonly_) return nullptr;
    FileHeader* hdr = header();
    if (index >= hdr->capacity) return nullptr;
    return static_cast<char*>(base_)
         + hdr->data_offset
         + index * slot_size_;
}

const IndexEntry* MmapFile::index_at(uint64_t index) const noexcept {
    const FileHeader* hdr = header();
    // 只有快照文件才有 index_offset；逐笔文件 index_offset == 0
    if (hdr->data_type != 0 || index >= hdr->capacity) return nullptr;
    const char* idx_ptr = static_cast<const char*>(base_)
                        + hdr->index_offset
                        + index * sizeof(IndexEntry);
    return reinterpret_cast<const IndexEntry*>(idx_ptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// 元信息
// ─────────────────────────────────────────────────────────────────────────────

uint64_t MmapFile::size() const noexcept {
    return header()->write_index.load(std::memory_order_acquire);
}

uint64_t MmapFile::capacity() const noexcept {
    return header()->capacity;
}

bool MmapFile::full() const noexcept {
    return size() >= capacity();
}

FileHeader* MmapFile::header() noexcept {
    return static_cast<FileHeader*>(base_);
}

const FileHeader* MmapFile::header() const noexcept {
    return static_cast<const FileHeader*>(base_);
}

void MmapFile::msync_header() noexcept {
    // 仅刷写文件头所在的首页；对 tmpfs（/dev/shm）实质无 IO
    ::msync(base_, kHeaderSize, MS_ASYNC);
}
