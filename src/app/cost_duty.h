#pragma once

#include <QVariantMap>

#include "core/stats_store.h"
#include "llm/llm_client.h"
#include "llm/llm_pricing.h"

/**
 * @file cost_duty.h
 * @brief Cost and statistics projections over the shared statistics section.
 */

namespace lens::app {

class CostDuty final {
public:
    /**
     * @brief Bind cost projections to statistics, model and pricing data.
     * @param stats The statistics section owned by StorageDuty.
     * @param llm Client whose active model selects the price row.
     * @param pricing Loaded price list.
     */
    CostDuty(core::StatsStore& stats, llm::LlmClient& llm, const llm::Pricing& pricing);

    /// @return Today's reading tallies and cost.
    QVariantMap stats() const;

    /// @return Cost and token projections for the current month, week and days.
    QVariantMap cost() const;

private:
    double amountOf(const core::DailyUsage& usage) const;

    core::StatsStore& stats_;
    llm::LlmClient& llm_;
    const llm::Pricing& pricing_;
};

}
