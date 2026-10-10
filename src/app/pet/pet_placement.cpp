/**
 * @file pet_placement.cpp
 * @brief Implementation of the pet window's placement rules.
 */

#include "app/pet/pet_placement.h"

namespace lens::app::pet {

bool fitsWithin(QPoint at, QSize size, QRect available)
{
    return available.contains(QRect{at, size});
}

QPoint bottomRightOf(QSize size, QRect available)
{
    return QPoint{available.right() - size.width() + 1, available.bottom() - size.height() + 1};
}

} // namespace lens::app::pet
