/** @file capture_policy.h
 * @brief Offline capture policy shared by the desktop hook and OCR pipeline.
 */
#pragma once

#include <cstdint>
#include <algorithm>

#include <QChar>
#include <QList>
#include <QString>

namespace lens::capture {

enum class DragSensitivity : std::uint8_t { Sensitive,
                                            Standard,
                                            Reluctant };

/// @return Physical pixel threshold for the selected drag tier.
constexpr int dragThreshold(DragSensitivity const sensitivity)
{
    switch (sensitivity) {
        case DragSensitivity::Sensitive: return 2;
        case DragSensitivity::Reluctant: return 8;
        default: return 4;
    }
}

/// @return Whether either axis reaches the threshold, including negative screen origins.
constexpr bool exceedsDragThreshold(std::int64_t const dx, std::int64_t const dy, int const threshold)
{
    return threshold > 0 and
           (dx >= threshold or dx <= -threshold or dy >= threshold or dy <= -threshold);
}

/// @return A valid prose length; malformed persisted values fall back to the default.
constexpr int minimumWordLength(int const value)
{
    return value >= 2 and value <= 5 ? value : 3;
}

/// @return The number of Unicode letter code points in an explicit selection.
inline int selectionLetterCount(QString const& text)
{
    const auto codepoints = text.toUcs4();
    return static_cast<int>(std::count_if(codepoints.cbegin(), codepoints.cend(), [](uint codepoint) {
        return QChar::isLetter(codepoint);
    }));
}

}
