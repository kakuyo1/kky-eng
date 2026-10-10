/**
 * @file pet_placement.cpp
 * @brief Implementation of the pet window's placement rules.
 */

#include "app/pet/pet_placement.h"

#include <algorithm>
#include <limits>

namespace lens::app::pet {

bool fitsWithin(QPoint at, QSize size, QRect available)
{
    return available.contains(QRect{at, size});
}

QPoint bottomRightOf(QSize size, QRect available)
{
    return QPoint{available.right() - size.width() + 1, available.bottom() - size.height() + 1};
}

QPoint placeWithin(QPoint at, QSize size, QList<QRect> const& areas, QRect primary)
{
    for (auto const& area : areas) {
        if (fitsWithin(at, size, area)) return at;
    }
    return bottomRightOf(size, primary);
}

int indexOfNearestArea(QPoint at, QList<QRect> const& areas)
{
    auto nearest  = -1;
    auto bestDist = std::numeric_limits<qint64>::max();
    for (qsizetype i = 0; i < areas.size(); ++i) {
        auto const& area = areas.at(i);
        if (area.contains(at)) return static_cast<int>(i);
        // Distance from the point to the rectangle along each axis; zero along an axis the point lies within.
        auto const dx   = std::max({area.left() - at.x(), 0, at.x() - area.right()});
        auto const dy   = std::max({area.top() - at.y(), 0, at.y() - area.bottom()});
        auto const dist = static_cast<qint64>(dx) * dx + static_cast<qint64>(dy) * dy;
        if (dist < bestDist) {
            bestDist = dist;
            nearest  = static_cast<int>(i);
        }
    }
    return nearest;
}

} // namespace lens::app::pet
