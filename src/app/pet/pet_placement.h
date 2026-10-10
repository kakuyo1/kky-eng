#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>

/**
 * @file pet_placement.h
 * @brief Where the pet window goes when the screen changed under it (PHASE3 3.5).
 */

namespace lens::app::pet {

/// @return Whether a window of `size` with its top-left at `at` lies wholly inside `available`.
bool fitsWithin(QPoint at, QSize size, QRect available);

/// @return The top-left corner that puts a window of `size` in the bottom-right of `available`.
QPoint bottomRightOf(QSize size, QRect available);

} // namespace lens::app::pet
