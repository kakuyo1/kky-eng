#pragma once

#include <compare>
#include <optional>
#include <string_view>

/**
 * @file semver.h
 * @brief Pure version parsing and comparison, with no Qt and no network.
 *
 * The update check compares a release tag against the version this build carries. Both sides
 * are three numbers, and nothing else is a version this program acts on: a pre-release or a
 * build suffix is refused rather than ordered, because Lens never ships one and a tag that
 * carries one says the release is not the plain successor of what the reader has.
 */

namespace lens::util {

/// @brief A release version, as the three numbers a tag carries.
struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;

    constexpr bool operator==(const SemVer&) const = default;
};

/// Orders by major, then minor, then patch: 1.10.0 is newer than 1.9.0.
constexpr std::strong_ordering operator<=>(const SemVer& left, const SemVer& right)
{
    if (const std::strong_ordering major = left.major <=> right.major; major != 0)
        return major;
    if (const std::strong_ordering minor = left.minor <=> right.minor; minor != 0)
        return minor;
    return left.patch <=> right.patch;
}

/**
 * @brief Parse a release tag into its three numbers.
 *
 * One leading `v` is stripped. The rest must be exactly three dot-separated components of
 * digits, each non-empty, with no suffix of any kind: `1.2.0-rc1` and `1.2.0+b1` are refused
 * rather than truncated, so a pre-release never reads as the plain version that precedes it.
 *
 * @param tag The tag as the source states it, such as `v1.2.0`.
 * @return The version, or nothing when the tag is not one this program accepts, including
 *         when a component does not fit in an `int`.
 */
std::optional<SemVer> parseTag(std::string_view tag);

}