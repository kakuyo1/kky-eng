#pragma once

#include <QDate>
#include <QObject>
#include <QVariantMap>

#include <functional>

#include "core/stats_store.h"
#include "llm/llm_client.h"
#include "llm/llm_pricing.h"

/**
 * @file cost_duty.h
 * @brief Cost and statistics projections over the shared statistics section.
 */

namespace lens::app {

/// @brief The result of evaluating the current local day's budget.
struct BudgetStatus {
    QDate date;
    double spent     = 0.0;
    double limit     = 0.0;
    double remaining = 0.0;
    bool limited     = false;
    bool exhausted   = false;
    QString currency;
};

class CostDuty final : public QObject {
    Q_OBJECT
public:
    using DateProvider = std::function<QDate()>;

    /**
     * @brief Bind cost projections to statistics, model and pricing data.
     * @param stats The statistics section owned by StorageDuty.
     * @param llm Client whose active model selects the price row.
     * @param pricing Loaded price list.
     */
    CostDuty(core::StatsStore& stats, llm::LlmClient& llm, const llm::Pricing& pricing, DateProvider dateProvider = [] { return QDate::currentDate(); }, QObject* parent = nullptr);

    /// @return Today's reading tallies and cost.
    QVariantMap stats() const;

    /// @return Cost and token projections for the current month, week and days.
    QVariantMap cost() const;

    /// @return The budget evaluated against the current local date.
    BudgetStatus budgetStatus() const;

    /// @return The budget evaluated against an explicit local date, for tests.
    BudgetStatus budgetStatusFor(const QDate& date) const;

    /// @return Whether a new explanation request may be sent.
    bool canRequest() const;

    /// @return Whether automatic scanning may produce new explanation work.
    bool canScan() const;

    /// @brief Update the persisted budget value in the shared document.
    /// @return False when the amount is invalid.
    bool setDailyBudget(double amount);

    /// @return True when a positive limit has been reached or exceeded.
    static bool budgetReached(double spent, double limit);

    /// @brief Set the model used only for legacy buckets migrated from old documents.
    void setLegacyModel(QString model);

    /// @brief Record response usage and notify projections that spending changed.
    void recordUsage(const std::string& minute, const llm::Usage& usage);

signals:
    /// @brief A budget, usage, or date-driven projection changed.
    void stateChanged();

private:
    double amountOf(const core::DailyUsage& usage) const;

    core::StatsStore& stats_;
    llm::LlmClient& llm_;
    const llm::Pricing& pricing_;
    DateProvider dateProvider_;
    QString legacyModel_;
};

}
