/**
 * @file cost_duty.cpp
 * @brief Cost and statistics projections over the shared statistics section.
 */

#include "cost_duty.h"

#include <QDate>

namespace lens::app {
namespace {

QString currencySymbol(const QString& code)
{
    if (code == QLatin1String("USD"))
        return QStringLiteral("$");
    if (code == QLatin1String("CNY"))
        return QString::fromUtf8("¥");
    return code;
}

}

CostDuty::CostDuty(core::StatsStore& stats, llm::LlmClient& llm, const llm::Pricing& pricing)
    : stats_(stats), llm_(llm), pricing_(pricing)
{
}

QVariantMap CostDuty::stats() const
{
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    const auto& daily   = stats_.daily();
    int todayPops       = 0;
    int todayLearned    = 0;
    int todayFresh      = 0;
    double todayCost    = 0.0;

    for (const auto& [date, usage] : daily) {
        if (date != today.toStdString())
            continue;
        todayPops    = usage.pops;
        todayLearned = usage.learned;
        todayFresh   = usage.fresh;
        todayCost    = amountOf(usage);
    }

    return QVariantMap{{"todayPops", todayPops},
                       {"todayLearned", todayLearned},
                       {"todayFresh", todayFresh},
                       {"todayCost", todayCost},
                       {"currency", currencySymbol(pricing_.currency())}};
}

QVariantMap CostDuty::cost() const
{
    const QDate today         = QDate::currentDate();
    double month              = 0.0;
    double todayAmount        = 0.0;
    double yesterday          = 0.0;
    double week               = 0.0;
    long long monthTokens     = 0;
    long long todayTokens     = 0;
    long long yesterdayTokens = 0;
    long long weekTokens      = 0;

    for (const auto& [date, usage] : stats_.daily()) {
        const QDate when = QDate::fromString(QString::fromStdString(date), Qt::ISODate);
        if (!when.isValid())
            continue;

        const double amount    = amountOf(usage);
        const long long tokens = usage.promptTokens + usage.completionTokens;
        if (when.year() == today.year() && when.month() == today.month()) {
            month += amount;
            monthTokens += tokens;
        }
        if (when == today) {
            todayAmount = amount;
            todayTokens = tokens;
        }
        if (when.daysTo(today) == 1) {
            yesterday       = amount;
            yesterdayTokens = tokens;
        }
        if (when.year() == today.year() && when.weekNumber() == today.weekNumber()) {
            week += amount;
            weekTokens += tokens;
        }
    }

    const double dailyAverage = today.day() > 0 ? month / today.day() : 0.0;
    return QVariantMap{{"month", month},
                       {"today", todayAmount},
                       {"yesterday", yesterday},
                       {"week", week},
                       {"dailyAverage", dailyAverage},
                       {"monthTokens", monthTokens},
                       {"todayTokens", todayTokens},
                       {"yesterdayTokens", yesterdayTokens},
                       {"weekTokens", weekTokens},
                       {"currency", currencySymbol(pricing_.currency())}};
}

double CostDuty::amountOf(const core::DailyUsage& usage) const
{
    return pricing_.cost(llm_.model(), llm::Usage{static_cast<int>(usage.promptTokens), static_cast<int>(usage.completionTokens)});
}

}
