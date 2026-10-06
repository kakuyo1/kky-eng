/**
 * @file cost_duty_test.cpp
 * @brief Daily budget, model attribution, migration, and persistence seams.
 */

#include <QCoreApplication>
#include <QDate>
#include <QUrl>
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <limits>

#include "app/cost_duty.h"
#include "app/storage_duty.h"
#include "core/known_store.h"

namespace {

using lens::app::CostDuty;
using lens::core::KnownStore;
using lens::core::StatsStore;

struct CostDutyTest : ::testing::Test {
    void SetUp() override
    {
        path = std::filesystem::temp_directory_path() / "lens_cost_duty_test.json";
        std::filesystem::remove(path);
    }

    void TearDown() override
    {
        std::filesystem::remove(path);
    }

    std::filesystem::path path;
};

struct QtApplication {
    int argc      = 1;
    char name[24] = "cost_duty_test";
    char* argv[2] = {name, nullptr};
    QCoreApplication application{argc, argv};
};

const QDate testDate()
{
    return QDate::fromString(QStringLiteral("2026-10-06"), Qt::ISODate);
}

const lens::llm::Pricing& pricing()
{
    static const lens::llm::Pricing loaded = lens::llm::Pricing::load(
        std::filesystem::path(LENS_SOURCE_DIR) / "data" / "llm" / "pricing.json");
    return loaded;
}

} // namespace

TEST(CostBudget, ReachesOnlyPositiveLimitsAtTheBoundary)
{
    EXPECT_FALSE(CostDuty::budgetReached(99.99, 0.0));
    EXPECT_FALSE(CostDuty::budgetReached(9.99, 10.0));
    EXPECT_TRUE(CostDuty::budgetReached(10.0, 10.0));
    EXPECT_TRUE(CostDuty::budgetReached(10.01, 10.0));
}

TEST_F(CostDutyTest, PersistsNumericBudgetAndKeepsUnknownDocumentKeys)
{
    QtApplication qt;
    auto store                    = KnownStore::load(path);
    store.document()["API-KEY"]   = "secret-is-not-a-log";
    store.document()["futureKey"] = nlohmann::json{{"keep", true}};
    lens::app::StorageDuty storage{store};

    storage.writeDocument("numericSetting", QVariant(1.25));
    ASSERT_TRUE(storage.statsStore().setDailyBudget(1.25));
    store.save();

    auto reloaded = KnownStore::load(path);
    EXPECT_TRUE(reloaded.document().at("dailyBudget").is_number());
    EXPECT_DOUBLE_EQ(reloaded.document().at("dailyBudget").get<double>(), 1.25);
    EXPECT_EQ(reloaded.document().at("numericSetting"), 1.25);
    EXPECT_EQ(reloaded.document().at("API-KEY"), "secret-is-not-a-log");
    EXPECT_EQ(reloaded.document().at("futureKey").at("keep"), true);

    lens::core::StatsStore stats(reloaded.document());
    EXPECT_FALSE(stats.setDailyBudget(-1.0));
    EXPECT_FALSE(stats.setDailyBudget(std::numeric_limits<double>::quiet_NaN()));
    EXPECT_DOUBLE_EQ(stats.dailyBudget(), 1.25);
}

TEST_F(CostDutyTest, MigratesLegacyDailyTokensToOneDeterministicBucket)
{
    QtApplication qt;
    auto store                = KnownStore::load(path);
    store.document()["daily"] = nlohmann::json{{"2026-10-06", {{"promptTokens", 1200}, {"completionTokens", 340}}}};
    StatsStore stats(store.document());

    const auto& day = stats.daily().at("2026-10-06");
    ASSERT_EQ(day.models.size(), 1u);
    EXPECT_EQ(day.models.at(lens::core::kLegacyUsageModel).promptTokens, 1200);
    EXPECT_EQ(day.models.at(lens::core::kLegacyUsageModel).completionTokens, 340);
}

TEST_F(CostDutyTest, AttributesEachResponseToItsCapturedModel)
{
    QtApplication qt;
    auto store = KnownStore::load(path);
    StatsStore stats(store.document());
    stats.setDailyBudget(0.000001);
    stats.recordUsage("2026-10-06 09:00", 1000000, 0, "deepseek-flash");
    stats.recordUsage("2026-10-06 09:01", 1000000, 0, "gpt-4.1-nano");

    lens::llm::LlmClient llm({QUrl{}, {}, QStringLiteral("gpt-4.1-nano")});
    CostDuty duty(stats, llm, pricing(), [] { return testDate(); });
    const double expected = pricing().cost("deepseek-flash", {1000000, 0, "deepseek-flash"}) + pricing().cost("gpt-4.1-nano", {1000000, 0, "gpt-4.1-nano"});

    EXPECT_DOUBLE_EQ(duty.budgetStatus().spent, expected);
    llm.setModel(QStringLiteral("deepseek-v4-pro"));
    EXPECT_DOUBLE_EQ(duty.budgetStatus().spent, expected);
}

TEST_F(CostDutyTest, ExactSpendPausesUntilTheNextDayAndBudgetRaiseRecovers)
{
    QtApplication qt;
    auto store = KnownStore::load(path);
    StatsStore stats(store.document());
    stats.setDailyBudget(0.01);
    stats.recordUsage("2026-10-06 09:00", 20000, 0, "deepseek-flash");

    QDate date = testDate();
    lens::llm::LlmClient llm({QUrl{}, {}, QStringLiteral("deepseek-flash")});
    CostDuty duty(stats, llm, pricing(), [&date] { return date; });
    ASSERT_TRUE(duty.budgetStatus().exhausted);
    EXPECT_FALSE(duty.canRequest());
    EXPECT_FALSE(duty.canScan());

    date = date.addDays(1);
    EXPECT_FALSE(duty.budgetStatus().exhausted);
    EXPECT_DOUBLE_EQ(duty.budgetStatus().spent, 0.0);
    EXPECT_TRUE(duty.canRequest());
    EXPECT_TRUE(duty.canScan());

    date = testDate();
    ASSERT_TRUE(duty.setDailyBudget(0.03));
    EXPECT_TRUE(duty.canRequest());
    EXPECT_TRUE(duty.canScan());
}
