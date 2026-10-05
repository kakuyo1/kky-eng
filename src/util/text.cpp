/**
 * @file text.cpp
 * @brief Implementations of Qt-free shared text operations.
 */

#include "util/text.h"

#include <algorithm>

namespace lens::util {

std::string datePart(std::string_view minute)
{
    constexpr std::size_t kDateLength = 10;
    if (minute.size() < kDateLength)
        return {};
    return std::string{minute.substr(0, kDateLength)};
}

int clampedAdd(int value, int delta)
{
    return std::max(0, value + delta);
}

}
