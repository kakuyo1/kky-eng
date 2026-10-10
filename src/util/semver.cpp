/**
 * @file semver.cpp
 * @brief Pure version parsing and comparison, with no Qt and no network.
 */

#include "semver.h"

#include <cstddef>
#include <limits>

namespace lens::util {
namespace {

/// @return One dot-separated component of @p text, or nothing when it is not digits.
///
/// The whole component has to be digits: `1.2.0rc` fails on the component, and `1.2.0-rc1`
/// fails on the empty string that follows the patch number, which is the same rejection for
/// both suffixes.
std::optional<int> parseComponent(std::string_view text)
{
    if (text.empty() or text.size() > 9) // Nine digits is the widest that cannot overflow below.
        return std::nullopt;

    long long value = 0;
    for (const char digit : text) {
        if (digit < '0' or digit > '9')
            return std::nullopt;
        value = value * 10 + (digit - '0');
    }
    if (value > std::numeric_limits<int>::max())
        return std::nullopt;
    return static_cast<int>(value);
}

} // namespace

std::optional<SemVer> parseTag(std::string_view tag)
{
    if (tag.size() > 1 and tag.front() == 'v')
        tag.remove_prefix(1);

    // Exactly three components: a fourth dot, or an empty trailing component left by a suffix
    // such as `1.2.0-rc1` after the patch number has been closed off, is not a version.
    const std::size_t firstDot = tag.find('.');
    if (firstDot == std::string_view::npos)
        return std::nullopt;
    const std::size_t secondDot = tag.find('.', firstDot + 1);
    if (secondDot == std::string_view::npos)
        return std::nullopt;
    if (tag.find('.', secondDot + 1) != std::string_view::npos)
        return std::nullopt;

    const auto major = parseComponent(tag.substr(0, firstDot));
    const auto minor = parseComponent(tag.substr(firstDot + 1, secondDot - firstDot - 1));
    const auto patch = parseComponent(tag.substr(secondDot + 1));
    if (not major or not minor or not patch)
        return std::nullopt;
    return SemVer{*major, *minor, *patch};
}

}