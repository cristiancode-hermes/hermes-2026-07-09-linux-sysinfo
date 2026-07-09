#ifndef SYSINFO_MMAP_READER_HPP
#define SYSINFO_MMAP_READER_HPP

/**
 * @file mmap_reader.hpp
 * @brief RAII wrapper around POSIX mmap / munmap for memory-mapped file I/O.
 *
 * mmap provides zero-copy access to file contents by mapping them
 * into the process address space. This is efficient for:
 *   - Large files read sequentially or randomly
 *   - Shared memory (MAP_SHARED)
 *   - Device memory with O_SYNC
 *
 * NOT suitable for:
 *   - /proc files (report st_size == 0, mmap fails with EINVAL)
 *   - sysfs files with dynamic content
 *   - Files that change size during mapping
 *
 * @see sysinfo::mmap::MmapReader
 */

#include "sysinfo/types.hpp"

#include <nlohmann/json.hpp>
#include <expected>
#include <string>
#include <string_view>
#include <filesystem>
#include <system_error>
#include <cerrno>
#include <cstring>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace sysinfo::mmap {

namespace fs = std::filesystem;

// ============================================================================
// MmapReader — RAII wrapper for mmap/munmap
// ============================================================================

/**
 * @brief RAII wrapper for memory-mapped file access.
 *
 * Opens a file, maps it into memory, and automatically unmaps on destruction.
 * Move-only (no copy — mapping handles are not shareable).
 *
 * Usage:
 * @code{.cpp}
 *   auto reader = MmapReader::open("/path/to/file");
 *   if (!reader) { return handle_error(); }
 *   std::string_view content{reader->data(), reader->size()};
 *   // reader unmaps automatically when it goes out of scope
 * @endcode
 */
class MmapReader {
public:
    /// Default constructor — creates an empty (non-mapped) reader.
    MmapReader() noexcept = default;

    /**
     * @brief Open and memory-map a file.
     *
     * @param path   Path to the file.
     * @param flags  mmap flags (default: MAP_PRIVATE).
     * @param prot   mmap protection (default: PROT_READ).
     * @return MmapReader on success, Error on failure.
     */
    [[nodiscard]]
    static Result<MmapReader> open(
        const fs::path& path,
        int prot  = PROT_READ,
        int flags = MAP_PRIVATE
    ) {
        // Open the file
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd == -1) {
            return std::unexpected(Error{
                errno, std::format("open({}) failed: {}", path.c_str(), std::strerror(errno))
            });
        }

        // Get file size
        struct stat st{};
        if (::fstat(fd, &st) == -1) {
            int saved_errno = errno;
            ::close(fd);
            return std::unexpected(Error{
                saved_errno,
                std::format("fstat({}) failed: {}", path.c_str(), std::strerror(saved_errno))
            });
        }

        // st_size == 0 check — /proc files and empty files can't be mmap'd
        if (st.st_size == 0) {
            ::close(fd);
            return std::unexpected(Error{
                EINVAL,
                std::format("mmap({}) failed: file size is 0 (is this a /proc file?)", path.c_str())
            });
        }

        // mmap with O_SYNC hint for device memory
        void* mapped = ::mmap(nullptr, static_cast<size_t>(st.st_size),
                              prot, flags, fd, 0);
        int saved_errno = errno;
        ::close(fd); // fd no longer needed after mmap

        if (mapped == MAP_FAILED) {
            return std::unexpected(Error{
                saved_errno,
                std::format("mmap({}, {} bytes) failed: {}",
                            path.c_str(), st.st_size, std::strerror(saved_errno))
            });
        }

        MmapReader reader;
        reader.data_ = static_cast<std::byte*>(mapped);
        reader.size_ = static_cast<size_t>(st.st_size);
        return reader;
    }

    /// Destructor — unmaps the memory.
    ~MmapReader() {
        destroy();
    }

    // No copy
    MmapReader(const MmapReader&) = delete;
    MmapReader& operator=(const MmapReader&) = delete;

    // Move
    MmapReader(MmapReader&& other) noexcept
        : data_(std::exchange(other.data_, nullptr))
        , size_(std::exchange(other.size_, 0))
    {}

    MmapReader& operator=(MmapReader&& other) noexcept {
        if (this != &other) {
            destroy();
            data_ = std::exchange(other.data_, nullptr);
            size_ = std::exchange(other.size_, 0);
        }
        return *this;
    }

    // ---- Accessors ----

    /// Get pointer to the mapped data.
    [[nodiscard]] const std::byte* data() const noexcept { return data_; }

    /// Get size of the mapped region in bytes.
    [[nodiscard]] size_t size() const noexcept { return size_; }

    /// Check if the reader has a valid mapping.
    [[nodiscard]] explicit operator bool() const noexcept { return data_ != nullptr; }

    /// Get a string_view over the mapped data.
    [[nodiscard]] std::string_view to_string_view() const {
        return {reinterpret_cast<const char*>(data_), size_};
    }

    /// Convert to JSON (hex dump for binary data).
    [[nodiscard]] nlohmann::json to_json(bool hex_dump = false) const {
        if (!data_) {
            return {{"mapped", false}, {"size", 0}};
        }
        if (hex_dump) {
            // Only hex dump first 64 bytes for display
            constexpr size_t max_dump = 64;
            auto dump_size = std::min(size_, max_dump);
            std::string hex;
            for (size_t i = 0; i < dump_size; ++i) {
                hex += std::format("{:02x}", static_cast<unsigned char>(data_[i]));
            }
            if (size_ > max_dump) hex += "...";
            return {{"mapped", true}, {"size", size_}, {"hex", hex}};
        }
        return {{"mapped", true}, {"size", size_}, {"preview", to_string_view().substr(0, 256)}};
    }

private:
    std::byte* data_ = nullptr;
    size_t     size_ = 0;

    void destroy() noexcept {
        if (data_ && size_ > 0) {
            ::munmap(data_, size_);
            data_ = nullptr;
            size_ = 0;
        }
    }
};

/**
 * @brief Read a file safely using mmap (falls back to read if mmap is not suitable).
 *
 * This function first tries mmap. If the file is a /proc virtual file (size == 0),
 * it falls back to read(). This provides the best of both worlds:
 * zero-copy for regular files, compatibility for virtual filesystems.
 */
[[nodiscard]]
inline Result<std::string> read_file_safe(const fs::path& path) {
    // Try mmap first
    auto mapped = MmapReader::open(path);
    if (mapped) {
        return std::string{mapped->to_string_view()};
    }

    // mmap failed — likely /proc or empty file. Fall back to read().
    std::ifstream ifs{path};
    if (!ifs.is_open()) {
        return std::unexpected(Error{
            ENOENT, std::format("Cannot open file: {}", path.c_str())
        });
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    if (ifs.bad()) {
        return std::unexpected(Error{
            EIO, std::format("Read error: {}", path.c_str())
        });
    }
    return oss.str();
}

} // namespace sysinfo::mmap

#endif // SYSINFO_MMAP_READER_HPP
