/**
 * @file cost_duty.cpp
 * @brief Cost and statistics projections over the shared statistics section.
 */

#include "cost_duty.h"

#include <QDate>

#include <algorithm>
#include <utility>

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

CostDuty::CostDuty(core::StatsStore& stats,
                   llm::LlmClient& llm,
                   const llm::Pricing& pricing,
                   DateProvider dateProvider,
                   QObject* parent)
    : QObject(parent),
      stats_(stats),
      llm_(llm),
      pricing_(pricing),
      dateProvider_(std::move(dateProvider)),
      legacyModel_(llm.model())
{
}

QVariantMap CostDuty::stats() const
{
    const BudgetStatus budget = budgetStatus();
    const std::string today   = budget.date.toString(Qt::ISODate).toStdString();
    const auto& daily         = stats_.daily();
    int todayPops             = 0;
    int todayLearned          = 0;
    int todayFresh            = 0;
    double todayCost          = 0.0;

    for (const auto& [date, usage] : daily) {
        if (date != today)
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
                       {"dailyBudget", budget.limit},
                       {"budgetSpent", budget.spent},
                       {"budgetRemaining", budget.remaining},
                       {"budgetLimited", budget.limited},
                       {"budgetExhausted", budget.exhausted},
                       {"budgetDate", budget.date.toString(Qt::ISODate)},
                       {"currency", currencySymbol(pricing_.currency())}};
}

QVariantMap CostDuty::cost() const
{
    const BudgetStatus budget = budgetStatus();
    const QDate today         = budget.date;
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
                       {"dailyBudget", budget.limit},
                       {"budgetSpent", budget.spent},
                       {"budgetRemaining", budget.remaining},
                       {"budgetLimited", budget.limited},
                       {"budgetExhausted", budget.exhausted},
                       {"budgetDate", budget.date.toString(Qt::ISODate)},
                       {"currency", currencySymbol(pricing_.currency())}};
}

double CostDuty::amountOf(const core::DailyUsage& usage) const
{
    if (usage.models.empty())
        return pricing_.cost(llm_.model(), llm::Usage{static_cast<int>(usage.promptTokens), static_cast<int>(usage.completionTokens), llm_.model()});

    double total = 0.0;
    for (const auto& [model, bucket] : usage.models) {
        const QString billingModel = model == core::kLegacyUsageModel ? legacyModel_ : QString::fromStdString(model);
        total += pricing_.cost(billingModel,
                               llm::Usage{static_cast<int>(bucket.promptTokens),
                                          static_cast<int>(bucket.completionTokens),
                                          billingModel});
    }
    return total;
}

BudgetStatus CostDuty::budgetStatus() const
{
    return budgetStatusFor(dateProvider_());
}

BudgetStatus CostDuty::budgetStatusFor(const QDate& date) const
{
    const QDate localDate = date.isValid() ? date : QDate::currentDate();
    const auto it         = stats_.daily().find(localDate.toString(Qt::ISODate).toStdString());
    const double spent    = it == stats_.daily().end() ? 0.0 : std::max(0.0, amountOf(it->second));
    const double limit    = stats_.dailyBudget();
    const bool limited    = limit > 0.0;
    const bool exhausted  = budgetReached(spent, limit);

    return BudgetStatus{.date      = localDate,
                        .spent     = spent,
                        .limit     = limit,
                        .remaining = limited ? std::max(0.0, limit - spent) : 0.0,
                        .limited   = limited,
                        .exhausted = exhausted,
                        .currency  = currencySymbol(pricing_.currency())};
}

bool CostDuty::canRequest() const
{
    return !budgetStatus().exhausted;
}

bool CostDuty::canScan() const
{
    return canRequest();
}

bool CostDuty::setDailyBudget(double amount)
{
    if (!stats_.setDailyBudget(amount))
        return false;
    emit stateChanged();
    return true;
}

bool CostDuty::budgetReached(double spent, double limit)
{
    return limit > 0.0 && spent >= limit;
}

void CostDuty::setLegacyModel(QString model)
{
    if (!model.trimmed().isEmpty())
        legacyModel_ = std::move(model);
}

void CostDuty::recordUsage(const std::string& minute, const llm::Usage& usage)
{
    stats_.recordUsage(minute, usage.promptTokens, usage.completionTokens, usage.model.toStdString());
    emit stateChanged();
}

}
