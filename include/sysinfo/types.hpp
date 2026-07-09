#ifndef SYSINFO_TYPES_HPP
#define SYSINFO_TYPES_HPP

/**
 * @file types.hpp
 * @brief Common type aliases and error handling for the sysinfo library.
 *
 * This file provides the foundational types used by all other headers.
 * It has NO dependencies on other project headers (no circular includes).
 */

#include <expected>
#include <string>
#include <format>
#include <system_error>
#include <cerrno>
#include <cstring>

namespace sysinfo {

/**
 * @brief Error type for system information operations.
 *
 * Carries a numeric error code (typically errno) and a human-readable message.
 */
struct Error {
    int         code{0};
    std::string message;
};

/**
 * @brief Result alias using std::expected.
 *
 * All sysinfo operations return either a value (T) or an Error.
 * This enables error handling without exceptions.
 *
 * Usage:
 * @code
 *   Result<int> r = try_something();
 *   if (!r) { handle(r.error()); }
 *   use(r.value());
 * @endcode
 *
 * @tparam T The success value type.
 */
template <typename T>
using Result = std::expected<T, Error>;

// Convenience: make_error helpers
[[nodiscard]] inline Error make_error(int code, std::string_view message) {
    return Error{code, std::string{message}};
}

[[nodiscard]] inline Error make_errno_error(std::string_view context) {
    return Error{errno, std::format("{}: {}", context, std::strerror(errno))};
}

} // namespace sysinfo

#endif // SYSINFO_TYPES_HPP
