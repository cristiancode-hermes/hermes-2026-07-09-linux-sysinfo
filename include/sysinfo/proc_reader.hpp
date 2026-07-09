#ifndef SYSINFO_PROC_READER_HPP
#define SYSINFO_PROC_READER_HPP

/**
 * @file proc_reader.hpp
 * @brief RAII reader for the Linux /proc filesystem.
 *
 * /proc is a virtual filesystem exposing kernel data structures.
 * IMPORTANT: /proc files report st_size == 0 and CANNOT be mmap'd.
 * Use read() / ifstream instead.
 *
 * /proc/cpuinfo uses "key\\t: value" format (tab before colon).
 * The key always includes a trailing tab/space that must be trimmed.
 *
 * @see sysinfo::proc::ProcReader
 */

#include "sysinfo/types.hpp"

#include <nlohmann/json.hpp>
#include <expected>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <ranges>
#include <algorithm>
#include <charconv>
#include <chrono>

namespace sysinfo::proc {

namespace fs = std::filesystem;

// ============================================================================
// Data structures
// ============================================================================

/// A single logical CPU / processor core.
struct CpuInfo {
    int         processor_id{};
    std::string vendor_id;
    std::string model_name;
    double      mhz{};
    int         cache_size_kb{};
    int         cores{};
    int         siblings{};

    [[nodiscard]] nlohmann::json to_json() const {
        return {
            {"processor",    processor_id},
            {"vendor",       vendor_id},
            {"model",        model_name},
            {"mhz",          mhz},
            {"cache_kb",     cache_size_kb},
            {"cores",        cores},
            {"siblings",     siblings}
        };
    }
};

/// Memory information from /proc/meminfo.
struct MemInfo {
    long long mem_total_kb{};
    long long mem_free_kb{};
    long long mem_available_kb{};
    long long buffers_kb{};
    long long cached_kb{};
    long long swap_total_kb{};
    long long swap_free_kb{};

    [[nodiscard]] nlohmann::json to_json() const {
        return {
            {"total_kb",     mem_total_kb},
            {"free_kb",      mem_free_kb},
            {"available_kb", mem_available_kb},
            {"used_kb",      mem_total_kb - mem_available_kb},
            {"buffers_kb",   buffers_kb},
            {"cached_kb",    cached_kb},
            {"swap_total_kb", swap_total_kb},
            {"swap_free_kb",  swap_free_kb}
        };
    }
};

/// A single process from /proc/[pid]/stat.
struct ProcessInfo {
    int         pid{};
    std::string comm;          // executable name
    char        state{};       // R, S, D, Z, T, etc.
    unsigned    utime{};       // user mode ticks
    unsigned    stime{};       // kernel mode ticks
    long        rss_pages{};   // resident set size (pages)
    std::string cmdline;       // full command line

    [[nodiscard]] nlohmann::json to_json() const {
        return {
            {"pid",     pid},
            {"name",    comm},
            {"state",   std::string{state}},
            {"utime",   utime},
            {"stime",   stime},
            {"rss_kb",  rss_pages * 4},  // approximate KB (4K pages)
            {"cmdline", cmdline}
        };
    }
};

// ============================================================================
// ProcReader — RAII wrapper for reading /proc files
// ============================================================================

/**
 * @brief RAII reader for the Linux /proc filesystem.
 *
 * All methods return Result<T> using std::expected.
 * Methods are stateless — read from /proc each time.
 */
class ProcReader {
public:
    /// Default constructor. No special setup needed.
    ProcReader() = default;

    // No copy, sí move
    ProcReader(const ProcReader&) = delete;
    ProcReader& operator=(const ProcReader&) = delete;
    ProcReader(ProcReader&&) noexcept = default;
    ProcReader& operator=(ProcReader&&) noexcept = default;

    // ---- High-level queries ----

    /// Parse /proc/cpuinfo → vector of CpuInfo.
    [[nodiscard]] Result<std::vector<CpuInfo>> read_cpu_info() const;

    /// Parse /proc/meminfo → MemInfo.
    [[nodiscard]] Result<MemInfo> read_mem_info() const;

    /// Scan /proc/[pid]/stat for all running processes.
    [[nodiscard]] Result<std::vector<ProcessInfo>> read_processes(int limit = 0) const;

    /// Read the full command line for a process from /proc/[pid]/cmdline.
    [[nodiscard]] Result<std::string> read_cmdline(int pid) const;

    /// Read system uptime in seconds from /proc/uptime.
    [[nodiscard]] Result<double> read_uptime() const;

    /// Read load average from /proc/loadavg.
    [[nodiscard]] Result<std::array<double, 3>> read_loadavg() const;

private:
    // ---- Helpers ----

    /// Read an entire /proc file into a string. Can NOT use mmap (procfs).
    [[nodiscard]] static Result<std::string> read_proc_file(const fs::path& path);

    /// Parse a key: value line, trimming trailing whitespace from key.
    [[nodiscard]] static std::pair<std::string_view, std::string_view>
    split_kv(std::string_view line);
};

// ============================================================================
// Inline implementations
// ============================================================================

inline Result<std::string> ProcReader::read_proc_file(const fs::path& path) {
    std::ifstream ifs{path};
    if (!ifs.is_open()) {
        return std::unexpected(Error{
            ENOENT, std::format("Cannot open: {}", path.c_str())
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

inline std::pair<std::string_view, std::string_view>
ProcReader::split_kv(std::string_view line) {
    // /proc/cpuinfo uses "key\t: value" format
    auto colon = line.find(':');
    if (colon == std::string_view::npos) {
        return {line, {}};
    }
    auto key = line.substr(0, colon);
    auto value = line.substr(colon + 1);

    // Trim trailing whitespace/tab from key (critical for /proc!)
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
        key.remove_suffix(1);
    }

    // Trim leading whitespace from value
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }

    return {key, value};
}

inline Result<std::vector<CpuInfo>> ProcReader::read_cpu_info() const {
    auto content = read_proc_file("/proc/cpuinfo");
    if (!content) return std::unexpected(content.error());

    std::vector<CpuInfo> cpus;
    CpuInfo current;

    for (auto line : std::string_view{*content} | std::views::split('\n')) {
        std::string_view sv{line.begin(), line.end()};
        if (sv.empty()) {
            // Empty line = end of a processor block
            if (current.processor_id > 0 || !current.vendor_id.empty()) {
                cpus.push_back(std::move(current));
                current = CpuInfo{};
            }
            continue;
        }
        auto [key, value] = split_kv(sv);
        if (key.empty()) continue;

        if      (key == "processor")   std::from_chars(value.data(), value.data() + value.size(), current.processor_id);
        else if (key == "vendor_id")   current.vendor_id = value;
        else if (key == "model name")  current.model_name = value;
        else if (key == "cpu MHz")     { double mhz{}; std::from_chars(value.data(), value.data() + value.size(), mhz); current.mhz = mhz; }
        else if (key == "cache size")  { std::from_chars(value.data(), value.data() + value.size(), current.cache_size_kb); }
        else if (key == "cpu cores")   { std::from_chars(value.data(), value.data() + value.size(), current.cores); }
        else if (key == "siblings")    { std::from_chars(value.data(), value.data() + value.size(), current.siblings); }
    }
    // Don't forget the last block
    if (current.processor_id > 0 || !current.vendor_id.empty()) {
        cpus.push_back(std::move(current));
    }

    if (cpus.empty()) {
        return std::unexpected(Error{ENODATA, "No CPU information found in /proc/cpuinfo"});
    }
    return cpus;
}

inline Result<MemInfo> ProcReader::read_mem_info() const {
    auto content = read_proc_file("/proc/meminfo");
    if (!content) return std::unexpected(content.error());

    MemInfo mem{};

    for (auto line : std::string_view{*content} | std::views::split('\n')) {
        std::string_view sv{line.begin(), line.end()};
        auto [key, value] = split_kv(sv);
        if (key.empty()) continue;

        // /proc/meminfo: "MemTotal:      16326484 kB"
        // Parse the first numeric token (strip "kB" suffix)
        auto space = value.find(' ');
        auto num_str = value.substr(0, space);

        long long val{};
        std::from_chars(num_str.data(), num_str.data() + num_str.size(), val);

        if      (key == "MemTotal")     mem.mem_total_kb = val;
        else if (key == "MemFree")      mem.mem_free_kb = val;
        else if (key == "MemAvailable") mem.mem_available_kb = val;
        else if (key == "Buffers")      mem.buffers_kb = val;
        else if (key == "Cached")       mem.cached_kb = val;
        else if (key == "SwapTotal")    mem.swap_total_kb = val;
        else if (key == "SwapFree")     mem.swap_free_kb = val;
    }

    if (mem.mem_total_kb == 0) {
        return std::unexpected(Error{ENODATA, "No memory information found in /proc/meminfo"});
    }
    return mem;
}

inline Result<std::vector<ProcessInfo>> ProcReader::read_processes(int limit) const {
    std::vector<ProcessInfo> processes;

    if (!fs::exists("/proc")) {
        return std::unexpected(Error{ENOENT, "/proc not found"});
    }

    for (auto const& entry : fs::directory_iterator("/proc")) {
        if (limit > 0 && static_cast<int>(processes.size()) >= limit) break;

        auto pid_str = entry.path().filename().string();
        // Only numeric directory names = processes
        bool is_pid = !pid_str.empty() && std::ranges::all_of(pid_str, [](char c) {
            return c >= '0' && c <= '9';
        });
        if (!is_pid) continue;

        int pid{};
        std::from_chars(pid_str.data(), pid_str.data() + pid_str.size(), pid);

        ProcessInfo proc{};
        proc.pid = pid;

        // Read /proc/[pid]/stat
        auto stat_path = entry.path() / "stat";
        auto stat_result = read_proc_file(stat_path);
        if (!stat_result) continue; // process may have exited — skip silently

        // Format: pid (comm) state ppid pgrp session tty_nr tpgid flags ...
        // comm is parenthesized and may contain spaces and )
        auto sv = std::string_view{*stat_result};
        auto paren1 = sv.find('(');
        auto paren2 = sv.rfind(')');  // last ) because comm may contain )
        if (paren1 == std::string_view::npos || paren2 == std::string_view::npos) continue;

        proc.comm = sv.substr(paren1 + 1, paren2 - paren1 - 1);
        if (paren2 + 2 < sv.size()) {
            proc.state = sv[paren2 + 2]; // char after ") "
        }

        // Parse utime, stime, rss from the remaining fields
        // After ") " we have: state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime stime cutime cstime rss
        auto remaining = sv.substr(paren2 + 2);
        auto fields = std::views::split(remaining, ' ');
        int field_idx = 0;
        for (auto f : fields) {
            std::string_view fv{f.begin(), f.end()};
            if (fv.empty()) continue;
            ++field_idx;
            if (field_idx == 12) { // utime (0-indexed from fields after comm)
                unsigned val{};
                std::from_chars(fv.data(), fv.data() + fv.size(), val);
                proc.utime = val;
            } else if (field_idx == 13) {
                unsigned val{};
                std::from_chars(fv.data(), fv.data() + fv.size(), val);
                proc.stime = val;
            } else if (field_idx == 23) {
                long val{};
                std::from_chars(fv.data(), fv.data() + fv.size(), val);
                proc.rss_pages = val;
            }
        }

        // Try to read cmdline (might be empty for kernel threads)
        auto cmdline_result = read_cmdline(pid);
        if (cmdline_result) {
            proc.cmdline = std::move(*cmdline_result);
        }

        processes.push_back(std::move(proc));
    }

    return processes;
}

inline Result<std::string> ProcReader::read_cmdline(int pid) const {
    auto path = std::format("/proc/{}/cmdline", pid);
    auto result = read_proc_file(path);
    if (!result) return std::unexpected(result.error());

    // cmdline uses NUL-separated arguments. Replace NUL with spaces.
    std::string cmdline = std::move(*result);
    std::ranges::replace(cmdline, '\0', ' ');
    // Trim trailing space
    while (!cmdline.empty() && cmdline.back() == ' ') {
        cmdline.pop_back();
    }
    return cmdline;
}

inline Result<double> ProcReader::read_uptime() const {
    auto content = read_proc_file("/proc/uptime");
    if (!content) return std::unexpected(content.error());

    double uptime_secs{};
    std::from_chars(content->data(), content->data() + content->size(), uptime_secs);
    return uptime_secs;
}

inline Result<std::array<double, 3>> ProcReader::read_loadavg() const {
    auto content = read_proc_file("/proc/loadavg");
    if (!content) return std::unexpected(content.error());

    std::array<double, 3> loads{};
    auto fields = std::views::split(std::string_view{*content}, ' ');
    int idx = 0;
    for (auto f : fields) {
        if (idx >= 3) break;
        std::string_view fv{f.begin(), f.end()};
        std::from_chars(fv.data(), fv.data() + fv.size(), loads[idx]);
        ++idx;
    }
    return loads;
}

} // namespace sysinfo::proc

#endif // SYSINFO_PROC_READER_HPP
