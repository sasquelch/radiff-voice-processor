#pragma once

#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace radiff::platform {

/// Locale-independent decimal number parsing ("1.5" always means one and a half,
/// regardless of the user's regional settings). Rejects trailing garbage.
/// Note: std::from_chars for floating point is not available on all supported
/// macOS deployment targets, hence the stream-based implementation.
inline std::optional<double> parseNumber(std::string_view text) {
    if (text.empty())
        return std::nullopt;
    std::istringstream in{std::string(text)};
    in.imbue(std::locale::classic());
    double value = 0.0;
    in >> value;
    if (in.fail())
        return std::nullopt;
    in >> std::ws;
    if (!in.eof())
        return std::nullopt;
    return value;
}

inline std::optional<long long> parseInteger(std::string_view text) {
    if (text.empty())
        return std::nullopt;
    std::istringstream in{std::string(text)};
    in.imbue(std::locale::classic());
    long long value = 0;
    in >> value;
    if (in.fail())
        return std::nullopt;
    in >> std::ws;
    if (!in.eof())
        return std::nullopt;
    return value;
}

} // namespace radiff::platform
