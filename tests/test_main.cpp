/**
 * @file test_main.cpp
 * @brief Unit and integration tests for linux-sysinfo.
 *
 * Uses Catch2 v3 for test discovery and assertions.
 *
 * Tests are organized in sections:
 *   1. ProcReader tests (/proc filesystem parsing)
 *   2. SysfsReader tests (block devices, network interfaces)
 *   3. MmapReader tests (memory-mapped file I/O)
 *   4. TerminalInfo tests (ioctl queries)
 *   5. JSON serialization tests
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sysinfo/sysinfo.hpp"
#include "sysinfo/types.hpp"
#include "sysinfo/proc_reader.hpp"
#include "sysinfo/sysfs_reader.hpp"
#include "sysinfo/mmap_reader.hpp"
#include "sysinfo/term_ioctl.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <cstdio>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;
using namespace sysinfo;

// ============================================================================
// Test utilities
// ============================================================================

/// Create a temporary file with given content, returning the path.
/// The file is deleted at the end of the scope.
struct TempFile {
    fs::path path;

    TempFile(std::string_view content, std::string_view extension = ".tmp") {
        path = fs::temp_directory_path() /
               std::format("sysinfo-test-{}{}", std::rand(), extension);
        std::ofstream ofs{path};
        ofs << content;
        ofs.close();
    }

    ~TempFile() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

// ============================================================================
// 1. ProcReader Tests
// ============================================================================

TEST_CASE("ProcReader reads /proc/cpuinfo", "[proc][linux]") {
    proc::ProcReader reader;
    auto result = reader.read_cpu_info();

    REQUIRE(result.has_value());

    // On any real Linux system, we should have at least 1 CPU
    REQUIRE_FALSE(result->empty());
    REQUIRE(result->at(0).processor_id >= 0);
    REQUIRE_FALSE(result->at(0).model_name.empty());

    // Log CPU info for inspection
    INFO("CPU count: " << result->size());
    INFO("First CPU: " << result->at(0).model_name << " @ " << result->at(0).mhz << " MHz");
}

TEST_CASE("ProcReader reads /proc/meminfo", "[proc][linux]") {
    proc::ProcReader reader;
    auto result = reader.read_mem_info();

    REQUIRE(result.has_value());
    REQUIRE(result->mem_total_kb > 0);
    REQUIRE(result->mem_free_kb > 0);
    REQUIRE(result->mem_total_kb > result->mem_free_kb); // total > free

    INFO("Memory total: " << result->mem_total_kb << " KB");
    INFO("Memory free:  " << result->mem_free_kb << " KB");
    INFO("Memory avail: " << result->mem_available_kb << " KB");
}

TEST_CASE("ProcReader reads /proc/uptime", "[proc][linux]") {
    proc::ProcReader reader;
    auto result = reader.read_uptime();

    REQUIRE(result.has_value());
    REQUIRE(*result > 0.0);
    REQUIRE(*result < 1e9); // less than ~30 years

    INFO("Uptime: " << *result << " seconds");
}

TEST_CASE("ProcReader reads /proc/loadavg", "[proc][linux]") {
    proc::ProcReader reader;
    auto result = reader.read_loadavg();

    REQUIRE(result.has_value());
    for (auto load : *result) {
        REQUIRE(load >= 0.0);
        REQUIRE(load < 1e6); // sanity check
    }
}

TEST_CASE("ProcReader lists processes", "[proc][linux]") {
    proc::ProcReader reader;
    auto result = reader.read_processes(10);

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->empty());

    // PID 1 should always exist (init)
    bool found_init = false;
    for (const auto& p : *result) {
        if (p.pid == 1) {
            found_init = true;
            REQUIRE_FALSE(p.comm.empty());
            break;
        }
    }
    REQUIRE(found_init);
}

TEST_CASE("ProcReader reads cmdline for PID 1", "[proc][linux]") {
    proc::ProcReader reader;
    auto result = reader.read_cmdline(1);

    // PID 1 might have empty cmdline (kernel-init) or a real one (systemd)
    // Either is fine — just check it doesn't error fatally
    if (result) {
        INFO("PID 1 cmdline: " << *result);
        REQUIRE_FALSE(result->empty());
    } else {
        // Kernel threads have no cmdline — acceptable
        WARN("PID 1 has no cmdline (kernel thread?)");
    }
}

// ============================================================================
// 2. SysfsReader Tests
// ============================================================================

TEST_CASE("SysfsReader lists block devices", "[sysfs][linux]") {
    sysfs::SysfsReader reader;
    auto result = reader.list_block_devices();

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->empty());

    // At minimum, one device should exist (root disk)
    bool has_disk = false;
    for (const auto& dev : *result) {
        if (dev.dev_type == "disk" && dev.size_sectors > 0) {
            has_disk = true;
            INFO("Disk: " << dev.name << " (" << dev.size_sectors * 512 / 1.0e9 << " GB)");
            break;
        }
    }
    REQUIRE(has_disk);
}

TEST_CASE("SysfsReader lists network interfaces", "[sysfs][linux]") {
    sysfs::SysfsReader reader;
    auto result = reader.list_network_interfaces();

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->empty());

    // At minimum, loopback (lo) should exist
    bool found_lo = false;
    for (const auto& net : *result) {
        if (net.name == "lo") {
            found_lo = true;
            REQUIRE(net.mac_address == "00:00:00:00:00:00");
            break;
        }
    }
    REQUIRE(found_lo);
}

// ============================================================================
// 3. MmapReader Tests
// ============================================================================

TEST_CASE("MmapReader opens and reads a regular file", "[mmap]") {
    TempFile tmp("Hello, mmap! This is a test file.\n");

    auto result = mmap::MmapReader::open(tmp.path);
    REQUIRE(result.has_value());

    REQUIRE(result->size() > 0);
    REQUIRE(result->data() != nullptr);

    auto sv = std::string{result->to_string_view()};
    REQUIRE_THAT(sv, Catch::Matchers::StartsWith("Hello, mmap!"));

    // Check to_json output
    auto j = result->to_json();
    REQUIRE(j["mapped"] == true);
    REQUIRE(j["size"].get<size_t>() > 0);
}

TEST_CASE("MmapReader rejects empty files (like /proc)", "[mmap]") {
    TempFile tmp(""); // empty file

    auto result = mmap::MmapReader::open(tmp.path);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == EINVAL);
}

TEST_CASE("MmapReader rejects non-existent files", "[mmap]") {
    auto result = mmap::MmapReader::open("/tmp/nonexistent-sysinfo-test-file");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("MmapReader is moveable but not copyable", "[mmap]") {
    TempFile tmp("movable content");

    auto r1 = mmap::MmapReader::open(tmp.path);
    REQUIRE(r1.has_value());

    // Move construct
    auto r2 = std::move(*r1);
    REQUIRE(r2.data() != nullptr);
    REQUIRE(r2.size() > 0);
    // r1 is now empty
    REQUIRE_FALSE(static_cast<bool>(*r1)); // r1.data() == nullptr after move
}

TEST_CASE("read_file_safe falls back to read() for empty files", "[mmap]") {
    // /proc/uptime is a virtual file — mmap should fail, read should work
    auto result = mmap::read_file_safe("/proc/uptime");
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->empty());
}

TEST_CASE("MmapReader JSON hex dump", "[mmap]") {
    TempFile tmp("\x48\x65\x6c\x6c\x6f"); // "Hello"

    auto result = mmap::MmapReader::open(tmp.path);
    REQUIRE(result.has_value());

    auto j = result->to_json(true);
    REQUIRE(j["mapped"] == true);
    REQUIRE(j.contains("hex"));
    REQUIRE_FALSE(j["hex"].get<std::string>().empty());
}

// ============================================================================
// 4. TerminalInfo Tests
// ============================================================================

TEST_CASE("TerminalInfo returns terminal size", "[terminal][ioctl]") {
    auto ws = term::TerminalInfo::get_winsize();

    // Even if not a TTY, we should get default values
    REQUIRE(ws.rows > 0);
    REQUIRE(ws.cols > 0);
    REQUIRE(ws.rows <= 999); // sanity check
    REQUIRE(ws.cols <= 999); // sanity check

    INFO("Terminal size: " << ws.cols << "x" << ws.rows
         << (ws.is_tty ? " (tty)" : " (not a tty)"));
}

TEST_CASE("TerminalInfo get_size convenience wrapper", "[terminal][ioctl]") {
    auto [rows, cols] = term::TerminalInfo::get_size();
    REQUIRE(rows > 0);
    REQUIRE(cols > 0);
}

TEST_CASE("Terminal attributes query", "[terminal][ioctl]") {
    auto result = term::TerminalInfo::get_attributes();

    // In a non-TTY environment (e.g., CI, pipe), this will fail
    if (result) {
        INFO("Terminal iflag: " << result->iflag);
        INFO("Terminal oflag: " << result->oflag);
    } else {
        // Not a TTY — skip
        WARN("Not a terminal, skipping attributes test");
    }
}

// ============================================================================
// 5. JSON Serialization Tests
// ============================================================================

TEST_CASE("CpuInfo JSON serialization roundtrip", "[json]") {
    proc::CpuInfo cpu;
    cpu.processor_id = 0;
    cpu.vendor_id = "GenuineIntel";
    cpu.model_name = "Test CPU";
    cpu.mhz = 2400.0;
    cpu.cache_size_kb = 8192;
    cpu.cores = 4;
    cpu.siblings = 8;

    auto j = cpu.to_json();
    REQUIRE(j["processor"] == 0);
    REQUIRE(j["vendor"] == "GenuineIntel");
    REQUIRE(j["model"]  == "Test CPU");

    // Roundtrip
    auto parsed = nlohmann::json::parse(j.dump());
    REQUIRE(parsed["processor"] == 0);
    REQUIRE(parsed["vendor"] == "GenuineIntel");
}

TEST_CASE("MemInfo JSON serialization", "[json]") {
    proc::MemInfo mem;
    mem.mem_total_kb = 16777216;
    mem.mem_free_kb = 8388608;
    mem.mem_available_kb = 12582912;

    auto j = mem.to_json();
    REQUIRE(j["total_kb"] == 16777216);
    REQUIRE(j["used_kb"] == 16777216 - 12582912); // total - available
}

TEST_CASE("BlockDevice JSON serialization", "[json]") {
    sysfs::BlockDevice dev;
    dev.name = "sda";
    dev.dev_type = "disk";
    dev.size_sectors = 234441648;
    dev.read_only = false;
    dev.removable = false;

    auto j = dev.to_json();
    REQUIRE(j["name"] == "sda");
    REQUIRE(j["type"] == "disk");
    REQUIRE(j["size_gb"].get<double>() == Catch::Approx(120.0).epsilon(0.1));
}

TEST_CASE("NetInterface JSON serialization", "[json]") {
    sysfs::NetInterface net;
    net.name = "eth0";
    net.mac_address = "aa:bb:cc:dd:ee:ff";
    net.oper_state = "up";
    net.mtu = 1500;
    net.speed_mbps = 1000;

    auto j = net.to_json();
    REQUIRE(j["name"] == "eth0");
    REQUIRE(j["mac"] == "aa:bb:cc:dd:ee:ff");
    REQUIRE(j["state"] == "up");
}

TEST_CASE("Winsize JSON serialization", "[json]") {
    term::Winsize ws;
    ws.rows = 24;
    ws.cols = 80;
    ws.is_tty = true;

    auto j = ws.to_json();
    REQUIRE(j["rows"] == 24);
    REQUIRE(j["cols"] == 80);
    REQUIRE(j["is_tty"] == true);
}

// ============================================================================
// 6. Error Handling Tests
// ============================================================================

TEST_CASE("Error type works with std::expected", "[error]") {
    auto success_fn = []() -> Result<int> { return 42; };
    auto fail_fn    = []() -> Result<int> {
        return std::unexpected(Error{ENOENT, "File not found"});
    };

    auto success = success_fn();
    REQUIRE(success.has_value());
    REQUIRE(*success == 42);

    auto fail = fail_fn();
    REQUIRE_FALSE(fail.has_value());
    REQUIRE(fail.error().code == ENOENT);
    REQUIRE_THAT(fail.error().message, Catch::Matchers::ContainsSubstring("not found"));
}

TEST_CASE("make_error helpers", "[error]") {
    auto e1 = make_error(EINVAL, "Bad argument");
    REQUIRE(e1.code == EINVAL);
    REQUIRE(e1.message == "Bad argument");

    auto e2 = make_errno_error("Context");
    // errno may be 0 if no error occurred before this call
    REQUIRE(e2.code >= 0);
}

// ============================================================================
// 7. High-level query tests
// ============================================================================

TEST_CASE("query_all returns JSON with all keys", "[integration][linux]") {
    auto result = sysinfo::query_all();
    REQUIRE(result.has_value());

    auto j = *result;
    REQUIRE(j.contains("cpu"));
    REQUIRE(j.contains("memory"));
    REQUIRE(j.contains("disks"));
    REQUIRE(j.contains("network"));
    REQUIRE(j.contains("terminal"));

    // CPU should have at least one processor
    REQUIRE(j["cpu"].is_array());
    REQUIRE_FALSE(j["cpu"].empty());

    // Memory should have total_kb > 0
    long long total_kb = j["memory"]["total_kb"].get<long long>();
    REQUIRE(total_kb > 0);
}

TEST_CASE("query_cpu returns CPU data", "[integration][linux]") {
    auto result = sysinfo::query_cpu();
    REQUIRE(result.has_value());
    REQUIRE(result->is_array());
    REQUIRE_FALSE(result->empty());
}

TEST_CASE("query_memory returns memory data", "[integration][linux]") {
    auto result = sysinfo::query_memory();
    REQUIRE(result.has_value());
    auto total_kb = (*result)["total_kb"].get<long long>();
    REQUIRE(total_kb > 0);
}

TEST_CASE("query_terminal always returns sensible defaults", "[integration]") {
    auto result = sysinfo::query_terminal();
    REQUIRE(result.has_value());
    auto rows = (*result)["rows"].get<int>();
    REQUIRE(rows > 0);
    auto cols = (*result)["cols"].get<int>();
    REQUIRE(cols > 0);
}
