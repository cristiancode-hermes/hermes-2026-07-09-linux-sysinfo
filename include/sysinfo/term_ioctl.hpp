#ifndef SYSINFO_TERM_IOCTL_HPP
#define SYSINFO_TERM_IOCTL_HPP

/**
 * @file term_ioctl.hpp
 * @brief Terminal information via ioctl(2).
 *
 * Uses Linux terminal ioctl calls to query the terminal device:
 *   - TIOCGWINSZ — get terminal window size (rows, cols, width, height in pixels)
 *   - TCGETS     — get terminal attributes (is this a TTY?)
 *
 * These operations require a file descriptor to the terminal (typically /dev/tty
 * or STDIN_FILENO). When running non-interactively (e.g., piped output, cron),
 * the terminal queries fall back to default values.
 *
 * @see sysinfo::term::TerminalInfo
 */

#include "sysinfo/types.hpp"

#include <nlohmann/json.hpp>
#include <expected>
#include <string>
#include <system_error>
#include <cerrno>
#include <cstring>

#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>

// Provide TIOCGWINSZ if not defined (should be in <sys/ioctl.h> on Linux)
#ifndef TIOCGWINSZ
#  define TIOCGWINSZ 0x5413
#endif

namespace sysinfo::term {

// ============================================================================
// Data structures
// ============================================================================

/// Terminal window size information.
struct Winsize {
    unsigned short rows{};    ///< Number of rows (lines).
    unsigned short cols{};    ///< Number of columns (characters).
    unsigned short width{};   ///< Width in pixels (may be 0 if unknown).
    unsigned short height{};  ///< Height in pixels (may be 0 if unknown).
    bool           is_tty{};  ///< True if stdin is a terminal.

    [[nodiscard]] nlohmann::json to_json() const {
        return {
            {"rows",    rows},
            {"cols",    cols},
            {"width",   width},
            {"height",  height},
            {"is_tty",  is_tty}
        };
    }
};

/// Terminal attributes.
struct TermAttributes {
    unsigned iflag{};  ///< Input flags
    unsigned oflag{};  ///< Output flags
    unsigned cflag{};  ///< Control flags
    unsigned lflag{};  ///< Local flags
    char     line{};   ///< Line discipline
    cc_t     cc[32]{}; ///< Control characters (variable length on Linux)

    [[nodiscard]] nlohmann::json to_json() const {
        return {
            {"iflag", iflag},
            {"oflag", oflag},
            {"cflag", cflag},
            {"lflag", lflag}
        };
    }
};

// ============================================================================
// TerminalInfo — terminal query functions
// ============================================================================

/**
 * @brief Terminal information accessor.
 *
 * All methods are static — no state needed.
 */
class TerminalInfo {
public:
    TerminalInfo() = delete; // static-only class

    /**
     * @brief Query terminal window size via TIOCGWINSZ ioctl.
     *
     * Uses stdin (STDIN_FILENO) to query the terminal. When stdout is
     * piped or redirected, falls back to the COLUMNS/LINES environment
     * variables, then to sensible defaults (80×24).
     */
    [[nodiscard]] static Winsize get_winsize() noexcept {
        Winsize ws{};

        // Check if stdin is a TTY
        ws.is_tty = (::isatty(STDIN_FILENO) == 1);

        if (ws.is_tty) {
            struct winsize w{};
            if (::ioctl(STDIN_FILENO, TIOCGWINSZ, &w) == 0) {
                ws.rows   = w.ws_row;
                ws.cols   = w.ws_col;
                ws.width  = w.ws_xpixel;
                ws.height = w.ws_ypixel;
            }

            // If ioctl returned zeros, try environment variables
            if (ws.rows == 0) {
                auto* lines = std::getenv("LINES");
                if (lines) ws.rows = static_cast<unsigned short>(std::atoi(lines));
            }
            if (ws.cols == 0) {
                auto* cols = std::getenv("COLUMNS");
                if (cols) ws.cols = static_cast<unsigned short>(std::atoi(cols));
            }
        }

        // Apply absolute defaults
        if (ws.rows == 0) ws.rows = 24;
        if (ws.cols == 0) ws.cols = 80;

        return ws;
    }

    /**
     * @brief Query terminal attributes via TCGETS ioctl.
     *
     * Returns the termios structure for stdin. Only meaningful when
     * stdin is a TTY; otherwise returns default-constructed attributes.
     */
    [[nodiscard]] static Result<TermAttributes> get_attributes() noexcept {
        if (::isatty(STDIN_FILENO) != 1) {
            return std::unexpected(Error{
                ENOTTY, "Not a terminal"
            });
        }

        struct termios t{};
        if (::ioctl(STDIN_FILENO, TCGETS, &t) == -1) {
            return std::unexpected(Error{
                errno, std::format("TCGETS ioctl failed: {}", std::strerror(errno))
            });
        }

        TermAttributes attrs{};
        attrs.iflag = t.c_iflag;
        attrs.oflag = t.c_oflag;
        attrs.cflag = t.c_cflag;
        attrs.lflag = t.c_lflag;
        attrs.line  = t.c_line;
        std::copy_n(t.c_cc, std::min(sizeof(t.c_cc) / sizeof(cc_t), sizeof(attrs.cc) / sizeof(cc_t)),
                    attrs.cc);

        return attrs;
    }

    /**
     * @brief Simple query: get terminal size as a pair (rows, cols).
     *
     * Convenience wrapper around get_winsize().
     */
    [[nodiscard]] static std::pair<unsigned short, unsigned short> get_size() noexcept {
        auto ws = get_winsize();
        return {ws.rows, ws.cols};
    }
};

} // namespace sysinfo::term

#endif // SYSINFO_TERM_IOCTL_HPP
