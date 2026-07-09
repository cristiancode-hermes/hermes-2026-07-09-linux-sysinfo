#ifndef SYSINFO_SYSFS_READER_HPP
#define SYSINFO_SYSFS_READER_HPP

/**
 * @file sysfs_reader.hpp
 * @brief RAII reader for the Linux sysfs virtual filesystem.
 *
 * sysfs exposes kernel objects as a hierarchy under /sys/.
 * This module provides access to:
 *   - /sys/block/     → block devices (disks, partitions)
 *   - /sys/class/net/ → network interfaces
 *
 * sysfs entries are regular files (unlike /proc) and can be mmap'd,
 * though parsing via ifstream is more practical for text attributes.
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

namespace sysinfo::sysfs {

namespace fs = std::filesystem;

// ============================================================================
// Data structures
// ============================================================================

/// A block device (disk, partition, loop, etc.).
struct BlockDevice {
    std::string name;        // e.g. "sda"
    std::string dev_type;    // e.g. "disk", "part", "loop"
    long long   size_sectors{}; // size in 512-byte sectors
    bool        read_only{};
    bool        removable{};
    std::string model;

    [[nodiscard]] nlohmann::json to_json() const {
        return {
            {"name",         name},
            {"type",         dev_type},
            {"size_sectors", size_sectors},
            {"size_bytes",   size_sectors * 512},
            {"size_gb",      static_cast<double>(size_sectors * 512) / 1.0e9},
            {"read_only",    read_only},
            {"removable",    removable},
            {"model",        model}
        };
    }
};

/// A network interface.
struct NetInterface {
    std::string name;         // e.g. "eth0", "wlan0"
    std::string mac_address;
    std::string oper_state;   // "up", "down", "unknown"
    unsigned    mtu{};
    std::string duplex;       // "full", "half", "unknown"
    long long   speed_mbps{}; // -1 if unknown

    [[nodiscard]] nlohmann::json to_json() const {
        return {
            {"name",       name},
            {"mac",        mac_address},
            {"state",      oper_state},
            {"mtu",        mtu},
            {"duplex",     duplex},
            {"speed_mbps", speed_mbps}
        };
    }
};

// ============================================================================
// SysfsReader — RAII wrapper for sysfs
// ============================================================================

/**
 * @brief RAII reader for the Linux sysfs filesystem.
 *
 * Reads /sys/block and /sys/class/net to enumerate devices.
 * All methods are stateless (re-read on every call).
 */
class SysfsReader {
public:
    SysfsReader() = default;

    SysfsReader(const SysfsReader&) = delete;
    SysfsReader& operator=(const SysfsReader&) = delete;
    SysfsReader(SysfsReader&&) noexcept = default;
    SysfsReader& operator=(SysfsReader&&) noexcept = default;

    /// List all block devices from /sys/block/.
    [[nodiscard]] Result<std::vector<BlockDevice>> list_block_devices() const;

    /// List all network interfaces from /sys/class/net/.
    [[nodiscard]] Result<std::vector<NetInterface>> list_network_interfaces() const;

private:
    /// Read a sysfs attribute file as a trimmed string.
    [[nodiscard]] static Result<std::string> read_attr(const fs::path& path);
};

// ============================================================================
// Inline implementations
// ============================================================================

inline Result<std::string> SysfsReader::read_attr(const fs::path& path) {
    std::ifstream ifs{path};
    if (!ifs.is_open()) {
        return std::unexpected(Error{
            ENOENT, std::format("Cannot open sysfs attr: {}", path.c_str())
        });
    }
    std::string value;
    std::getline(ifs, value);
    // Trim trailing whitespace (sysfs values often end with newline)
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

inline Result<std::vector<BlockDevice>> SysfsReader::list_block_devices() const {
    const fs::path sys_block{"/sys/block"};
    if (!fs::exists(sys_block) || !fs::is_directory(sys_block)) {
        return std::unexpected(Error{ENOENT, "/sys/block not found"});
    }

    std::vector<BlockDevice> devices;

    for (auto const& entry : fs::directory_iterator(sys_block)) {
        auto name = entry.path().filename().string();
        if (name.empty()) continue;

        BlockDevice dev;
        dev.name = name;

        // Determine type: partitions have a "partition" file
        if (fs::exists(entry.path() / "partition")) {
            dev.dev_type = "part";
        } else {
            dev.dev_type = "disk";
        }

        // Read size (in 512-byte sectors)
        auto size_result = read_attr(entry.path() / "size");
        if (size_result) {
            std::from_chars(size_result->data(), size_result->data() + size_result->size(), dev.size_sectors);
        }

        // Read-only flag
        auto ro_result = read_attr(entry.path() / "ro");
        if (ro_result) {
            dev.read_only = (*ro_result == "1");
        }

        // Removable flag
        auto rem_result = read_attr(entry.path() / "removable");
        if (rem_result) {
            dev.removable = (*rem_result == "1");
        }

        // Model (may not exist for all devices)
        auto model_result = read_attr(entry.path() / "device" / "model");
        if (model_result) {
            dev.model = std::move(*model_result);
        }

        devices.push_back(std::move(dev));
    }

    // Sort by name for deterministic output
    std::ranges::sort(devices, {}, &BlockDevice::name);

    return devices;
}

inline Result<std::vector<NetInterface>> SysfsReader::list_network_interfaces() const {
    const fs::path sys_net{"/sys/class/net"};
    if (!fs::exists(sys_net) || !fs::is_directory(sys_net)) {
        return std::unexpected(Error{ENOENT, "/sys/class/net not found"});
    }

    std::vector<NetInterface> interfaces;

    for (auto const& entry : fs::directory_iterator(sys_net)) {
        auto name = entry.path().filename().string();
        if (name.empty()) continue;

        NetInterface net;
        net.name = name;

        // MAC address
        auto addr_result = read_attr(entry.path() / "address");
        if (addr_result) {
            net.mac_address = std::move(*addr_result);
        }

        // Operational state
        auto state_result = read_attr(entry.path() / "operstate");
        if (state_result) {
            net.oper_state = std::move(*state_result);
        }

        // MTU
        auto mtu_result = read_attr(entry.path() / "mtu");
        if (mtu_result) {
            unsigned mtu{};
            std::from_chars(mtu_result->data(), mtu_result->data() + mtu_result->size(), mtu);
            net.mtu = mtu;
        }

        // Duplex & speed (only for physical interfaces)
        auto duplex_result = read_attr(entry.path() / "duplex");
        if (duplex_result) {
            net.duplex = std::move(*duplex_result);
        }

        auto speed_result = read_attr(entry.path() / "speed");
        if (speed_result) {
            long long speed{};
            std::from_chars(speed_result->data(), speed_result->data() + speed_result->size(), speed);
            net.speed_mbps = speed;
        } else {
            net.speed_mbps = -1;
        }

        interfaces.push_back(std::move(net));
    }

    std::ranges::sort(interfaces, {}, &NetInterface::name);

    return interfaces;
}

} // namespace sysinfo::sysfs

#endif // SYSINFO_SYSFS_READER_HPP
