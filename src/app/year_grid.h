#pragma once

#include <QDate>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <map>
#include <string>

#include "core/stats_store.h"

/**
 * @file year_grid.h
 * @brief The 365-day window the annual words surface draws.
 *
 * AppController answered this from QDate::currentDate(), so its cases could only check the shape
 * of whichever day they happened to run on -- never a leap year, and never a year with a single
 * recorded day. The window is a pure projection of a date and the daily tallies, so it lives
 * here where a case can hand it the date it means; PHASE2 section 5.3 names both of those.
 */

namespace lens::app {

/**
 * @brief The 365 local days ending at @p today, oldest first.
 * @param today The last day of the window; the caller reads it from the clock.
 * @param daily Per-date tallies keyed "yyyy-MM-dd". Dates it does not carry are zero-use days.
 * @return 365 maps of {date, pops, learned, fresh}. A day with nothing recorded is present with
 *         zero tallies rather than left out: the surface draws a square for every one of them.
 */
inline QVariantList yearGrid(const QDate& today, const std::map<std::string, core::DailyUsage>& daily)
{
    QVariantList out;
    out.reserve(365);

    for (int offset = 364; offset >= 0; --offset) {
        const QDate date             = today.addDays(-offset);
        const std::string key        = date.toString(QStringLiteral("yyyy-MM-dd")).toStdString();
        const auto day               = daily.find(key);
        const core::DailyUsage usage = day == daily.end() ? core::DailyUsage{} : day->second;
        out.append(QVariantMap{{"date", QString::fromStdString(key)},
                               {"pops", usage.pops},
                               {"learned", usage.learned},
                               {"fresh", usage.fresh}});
    }
    return out;
}

}
