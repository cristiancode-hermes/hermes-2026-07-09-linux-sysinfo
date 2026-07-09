#ifndef SYSINFO_SYSINFO_HPP
#define SYSINFO_SYSINFO_HPP

/**
 * @file sysinfo.hpp
 * @brief Umbrella header for the Linux System Information library.
 *
 * Includes all sub-readers and provides high-level query functions
 * that aggregate data from /proc, sysfs, mmap, and ioctl sources.
 *
 * All public types live under the sysinfo namespace.
 * Error handling uses std::expected (C++23 via <expected>).
 */

#include "types.hpp"
#include "proc_reader.hpp"
#include "sysfs_reader.hpp"
#include "mmap_reader.hpp"
#include "term_ioctl.hpp"

#include <nlohmann/json.hpp>

namespace sysinfo {

// ============================================================================
// High-level query functions — individual queries first, then query_all()
// ============================================================================

/**
 * @brief Query CPU information.
 *
 * Returns a JSON array of logical processors with: model, cores, mhz, cache.
 */
inline Result<nlohmann::json> query_cpu() {
    proc::ProcReader reader;
    auto cpus = reader.read_cpu_info();
    if (!cpus) return std::unexpected(cpus.error());

    nlohmann::json arr = nlohmann::json::array();
    for (auto& cpu : *cpus) {
        arr.push_back(cpu.to_json());
    }
    return arr;
}

/**
 * @brief Query memory information.
 *
 * Returns a JSON object with: total, free, available, used, swap, plus uptime/load.
 */
inline Result<nlohmann::json> query_memory() {
    proc::ProcReader reader;
    auto mem = reader.read_mem_info();
    if (!mem) return std::unexpected(mem.error());

    auto result = mem->to_json();

    // Add uptime and load average as bonus
    auto uptime = reader.read_uptime();
    if (uptime) result["uptime_secs"] = *uptime;

    auto load = reader.read_loadavg();
    if (load) {
        result["load_1m"]  = (*load)[0];
        result["load_5m"]  = (*load)[1];
        result["load_15m"] = (*load)[2];
    }

    return result;
}

/**
 * @brief Query block device information (via sysfs).
 */
inline Result<nlohmann::json> query_disks() {
    sysfs::SysfsReader reader;
    auto devices = reader.list_block_devices();
    if (!devices) return std::unexpected(devices.error());

    nlohmann::json arr = nlohmann::json::array();
    for (auto& dev : *devices) {
        arr.push_back(dev.to_json());
    }
    return arr;
}

/**
 * @brief Query network interface information (via sysfs).
 */
inline Result<nlohmann::json> query_network() {
    sysfs::SysfsReader reader;
    auto interfaces = reader.list_network_interfaces();
    if (!interfaces) return std::unexpected(interfaces.error());

    nlohmann::json arr = nlohmann::json::array();
    for (auto& net : *interfaces) {
        arr.push_back(net.to_json());
    }
    return arr;
}

/**
 * @brief Query running processes (snapshot via /proc).
 *
 * @param limit  Maximum number of processes to return (0 = no limit).
 */
inline Result<nlohmann::json> query_processes(int limit) {
    proc::ProcReader reader;
    auto processes = reader.read_processes(limit);
    if (!processes) return std::unexpected(processes.error());

    nlohmann::json arr = nlohmann::json::array();
    for (auto& p : *processes) {
        arr.push_back(p.to_json());
    }
    return arr;
}

/**
 * @brief Query terminal information (via ioctl TIOCGWINSZ).
 */
inline Result<nlohmann::json> query_terminal() {
    auto ws = term::TerminalInfo::get_winsize();
    return ws.to_json();
}

/**
 * @brief Gather all system information into a single JSON object.
 *
 * Equivalent to calling each individual query function and merging results.
 * Returns a JSON object with keys: cpu, memory, disks, network, terminal.
 */
inline Result<nlohmann::json> query_all() {
    nlohmann::json result;

    auto cpu = query_cpu();
    if (cpu) result["cpu"] = std::move(*cpu);

    auto mem = query_memory();
    if (mem) result["memory"] = std::move(*mem);

    auto disks = query_disks();
    if (disks) result["disks"] = std::move(*disks);

    auto net = query_network();
    if (net) result["network"] = std::move(*net);

    auto term = query_terminal();
    if (term) result["terminal"] = std::move(*term);

    auto proc = query_processes(20);
    if (proc) result["processes"] = std::move(*proc);

    return result;
}

} // namespace sysinfo

#endif // SYSINFO_SYSINFO_HPP
