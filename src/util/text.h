#pragma once

#include <string>
#include <string_view>

/**
 * @file text.h
 * @brief Small Qt-free text operations shared by Lens modules.
 */

namespace lens::util {

/**
 * @brief Return the date portion of a stored local minute.
 * @param minute A timestamp in `YYYY-MM-DD HH:MM` form.
 * @return The first ten characters, or an empty string when the input is too short.
 */
std::string datePart(std::string_view minute);

/**
 * @brief Add a signed tally delta without allowing a negative count.
 * @param value Existing non-negative count.
 * @param delta Change to apply.
 * @return The changed count, clamped at zero.
 */
int clampedAdd(int value, int delta);

}
