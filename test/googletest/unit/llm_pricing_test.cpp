/**
 * @file llm_pricing_test.cpp
 * @brief The price list: it loads, it refuses a broken one, and it converts tokens to money.
 *
 * The rates pin data/llm/pricing.json, so a data edit that drops a model or a currency fails
 * here instead of quietly showing zero on the cost surface.
 */

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "llm/llm_pricing.h"
#include "support.h"

namespace {

using lens::llm::Pricing;
using lens::llm::Usage;

std::filesystem::path pricingPath()
{
    return lens::test::sourceDir() / "data" / "llm" / "pricing.json";
}

/// @brief A temp file holding @p body, removed when the test ends.
struct TempPricing {
    explicit TempPricing(const char* body)
        : path(std::filesystem::temp_directory_path() / "lens_pricing_test.json")
    {
        std::ofstream(path) << body;
    }
    ~TempPricing()
    {
        std::filesystem::remove(path);
    }
    std::filesystem::path path;
};

}

TEST(PricingTest, LoadsTheShippedPriceList)
{
    const Pricing pricing = Pricing::load(pricingPath());

    EXPECT_EQ(pricing.currency(), QStringLiteral("USD"));
    // 1M prompt tokens at 0.15 per million, plus 1M completion at 0.6.
    const double cost = pricing.cost(QStringLiteral("deepseek-flash"), Usage{1000000, 1000000});
    EXPECT_DOUBLE_EQ(cost, 0.75);
}

TEST(PricingTest, ScalesLinearlyWithTokens)
{
    const Pricing pricing = Pricing::load(pricingPath());

    const double small = pricing.cost(QStringLiteral("deepseek-flash"), Usage{1000, 200});
    EXPECT_DOUBLE_EQ(small, 1000 * 0.15 / 1000000 + 200 * 0.6 / 1000000);
}

TEST(PricingTest, CostsAnUnlistedModelAtZeroRatherThanGuessing)
{
    const Pricing pricing = Pricing::load(pricingPath());

    EXPECT_DOUBLE_EQ(pricing.cost(QStringLiteral("no-such-model"), Usage{1000000, 1000000}), 0.0);
}

TEST(PricingTest, RefusesABrokenPriceList)
{
    const TempPricing noCurrency(R"({"unit":1000000,"models":{"m":{"input":1,"output":2}}})");
    const TempPricing noModels(R"({"currency":"USD","unit":1000000,"models":{}})");
    const TempPricing notJson("not json");

    EXPECT_THROW(Pricing::load(noCurrency.path), std::runtime_error);
    EXPECT_THROW(Pricing::load(noModels.path), std::runtime_error);
    EXPECT_THROW(Pricing::load(notJson.path), std::runtime_error);
    EXPECT_THROW(Pricing::load(std::filesystem::path("does-not-exist.json")), std::runtime_error);
}
