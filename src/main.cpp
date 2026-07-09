/**
 * @file main.cpp
 * @brief linux-sysinfo — Linux System Information Tool
 *
 * Gathers system information via /proc, sysfs, mmap, and ioctl,
 * and outputs it as formatted text or JSON.
 *
 * Usage:
 *   linux-sysinfo [--json] [--pretty] [--cpu] [--memory] [--disks] [--network] [--processes] [--all]
 *
 * Examples:
 *   linux-sysinfo --all --pretty      # All info as pretty JSON
 *   linux-sysinfo --cpu --memory      # CPU and memory info only
 *   linux-sysinfo --disks --pretty    # Disk info as pretty JSON
 */

#include "sysinfo/sysinfo.hpp"

#include <iostream>
#include <print>
#include <string>
#include <string_view>
#include <vector>
#include <format>
#include <cstdlib>

// ============================================================================
// Output formatters
// ============================================================================

namespace {

void print_text(const nlohmann::json& j, const std::string& section) {
    std::println("--- {} ---", section);

    if (j.is_array()) {
        for (const auto& item : j) {
            for (auto it = item.begin(); it != item.end(); ++it) {
                std::println("  {:>20}: {}", it.key(), it.value().dump());
            }
            std::println("");
        }
    } else if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.value().is_structured()) {
                std::println("  {}:", it.key());
                for (auto inner = it.value().begin(); inner != it.value().end(); ++inner) {
                    std::println("    {:>16}: {}", inner.key(), inner.value().dump());
                }
            } else {
                std::println("  {:>20}: {}", it.key(), it.value().dump());
            }
        }
    }
}

void print_error(const sysinfo::Error& err, const std::string& context) {
    std::println(stderr, "ERROR [{}]: {} -- {}", context, err.message, std::strerror(err.code));
}

// ── Argument parsing (manual — no external CLI lib needed) ──

struct Args {
    bool show_all      = false;
    bool show_cpu      = false;
    bool show_memory   = false;
    bool show_disks    = false;
    bool show_network  = false;
    bool show_processes = false;
    bool show_terminal = false;
    bool json_output   = true;   // default to JSON
    bool pretty_print  = true;   // default to pretty
    bool help          = false;
};

Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};
        if (arg == "--all" || arg == "-a") {
            args.show_all = true;
        } else if (arg == "--cpu") {
            args.show_cpu = true;
        } else if (arg == "--memory" || arg == "--mem") {
            args.show_memory = true;
        } else if (arg == "--disks" || arg == "--disk") {
            args.show_disks = true;
        } else if (arg == "--network" || arg == "--net") {
            args.show_network = true;
        } else if (arg == "--processes" || arg == "--procs") {
            args.show_processes = true;
        } else if (arg == "--terminal" || arg == "--tty") {
            args.show_terminal = true;
        } else if (arg == "--json") {
            args.json_output = true;
        } else if (arg == "--pretty") {
            args.pretty_print = true;
        } else if (arg == "--text") {
            args.json_output = false;
        } else if (arg == "--help" || arg == "-h") {
            args.help = true;
        }
    }
    return args;
}

void print_usage(const char* prog) {
    std::println("{} — Linux System Information Tool", prog);
    std::println("");
    std::println("Usage:");
    std::println("  {} [OPTIONS]", prog);
    std::println("");
    std::println("Options:");
    std::println("  --all, -a           Show all available information");
    std::println("  --cpu               Show CPU information");
    std::println("  --memory, --mem     Show memory information");
    std::println("  --disks, --disk     Show block device information");
    std::println("  --network, --net    Show network interface information");
    std::println("  --processes, --procs Show process list");
    std::println("  --terminal, --tty   Show terminal information");
    std::println("  --json              Output as JSON (default)");
    std::println("  --pretty            Pretty-print JSON (default)");
    std::println("  --text              Output as plain text");
    std::println("  --help, -h          Show this help message");
    std::println("");
    std::println("If no category is specified, --all is assumed.");
    std::println("Examples:");
    std::println("  {{}} --cpu --memory    # CPU and memory");
    std::println("  {{}} --all --pretty   # Everything as pretty JSON");
}

} // anonymous namespace

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    auto args = parse_args(argc, argv);

    if (args.help) {
        print_usage(argv[0]);
        return 0;
    }

    // If no category specified, default to --all
    bool any_category = args.show_cpu || args.show_memory || args.show_disks
                        || args.show_network || args.show_processes || args.show_terminal;
    if (args.show_all || !any_category) {
        args.show_cpu = args.show_memory = args.show_disks
            = args.show_network = args.show_processes = args.show_terminal = true;
    }

    nlohmann::json output = nlohmann::json::object();

    if (args.show_cpu) {
        auto result = sysinfo::query_cpu();
        if (result) {
            output["cpu"] = std::move(*result);
        } else {
            print_error(result.error(), "cpu");
            output["cpu"] = {{"error", result.error().message}};
        }
    }

    if (args.show_memory) {
        auto result = sysinfo::query_memory();
        if (result) {
            output["memory"] = std::move(*result);
        } else {
            print_error(result.error(), "memory");
            output["memory"] = {{"error", result.error().message}};
        }
    }

    if (args.show_disks) {
        auto result = sysinfo::query_disks();
        if (result) {
            output["disks"] = std::move(*result);
        } else {
            print_error(result.error(), "disks");
            output["disks"] = {{"error", result.error().message}};
        }
    }

    if (args.show_network) {
        auto result = sysinfo::query_network();
        if (result) {
            output["network"] = std::move(*result);
        } else {
            print_error(result.error(), "network");
            output["network"] = {{"error", result.error().message}};
        }
    }

    if (args.show_processes) {
        // Show top 30 processes by default
        auto result = sysinfo::query_processes(30);
        if (result) {
            output["processes"] = std::move(*result);
        } else {
            print_error(result.error(), "processes");
            output["processes"] = {{"error", result.error().message}};
        }
    }

    if (args.show_terminal) {
        auto result = sysinfo::query_terminal();
        if (result) {
            output["terminal"] = std::move(*result);
        } else {
            print_error(result.error(), "terminal");
            output["terminal"] = {{"error", result.error().message}};
        }
    }

    // ── Output ──
    if (args.json_output) {
        if (args.pretty_print) {
            std::println("{}", output.dump(2));
        } else {
            std::println("{}", output.dump());
        }
    } else {
        // Plain text output
        for (auto it = output.begin(); it != output.end(); ++it) {
            print_text(it.value(), it.key());
        }
    }

    return 0;
}
