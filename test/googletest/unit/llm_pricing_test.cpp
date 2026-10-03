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

    // The vendor quotes in USD and the surfaces show CNY, so both numbers below are the
    // converted ones: this is what the cost popup prints.
    EXPECT_EQ(pricing.currency(), QStringLiteral("CNY"));
    // 1M prompt tokens at 0.15 per million, plus 1M completion at 0.6, at the file's rate.
    const double cost = pricing.cost(QStringLiteral("deepseek-flash"), Usage{1000000, 1000000});
    EXPECT_DOUBLE_EQ(cost, (0.15 + 0.6) * 6.71);
}

TEST(PricingTest, ScalesLinearlyWithTokens)
{
    const Pricing pricing = Pricing::load(pricingPath());

    const double small = pricing.cost(QStringLiteral("deepseek-flash"), Usage{1000, 200});
    EXPECT_DOUBLE_EQ(small, (1000 * 0.15 / 1000000 + 200 * 0.6 / 1000000) * 6.71);
}

TEST(PricingTest, ShowsTheVendorsCurrencyWhenNoDisplayBlockIsGiven)
{
    const TempPricing plain(R"({"currency":"EUR","unit":1000000,"models":{"m":{"input":1,"output":2}}})");
    const Pricing pricing = Pricing::load(plain.path);

    EXPECT_EQ(pricing.currency(), QStringLiteral("EUR"));
    EXPECT_DOUBLE_EQ(pricing.cost(QStringLiteral("m"), Usage{1000000, 1000000}), 3.0);
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
    // A display block that cannot convert: a zero rate would show every cost as free.
    const TempPricing noRate(R"({"currency":"USD","display":{"currency":"CNY"},"unit":1000000,"models":{"m":{"input":1,"output":2}}})");
    const TempPricing zeroRate(R"({"currency":"USD","display":{"currency":"CNY","multiplier":0},"unit":1000000,"models":{"m":{"input":1,"output":2}}})");
    const TempPricing noCode(R"({"currency":"USD","display":{"multiplier":6.7},"unit":1000000,"models":{"m":{"input":1,"output":2}}})");

    EXPECT_THROW(Pricing::load(noCurrency.path), std::runtime_error);
    EXPECT_THROW(Pricing::load(noModels.path), std::runtime_error);
    EXPECT_THROW(Pricing::load(notJson.path), std::runtime_error);
    EXPECT_THROW(Pricing::load(noRate.path), std::runtime_error);
    EXPECT_THROW(Pricing::load(zeroRate.path), std::runtime_error);
    EXPECT_THROW(Pricing::load(noCode.path), std::runtime_error);
    EXPECT_THROW(Pricing::load(std::filesystem::path("does-not-exist.json")), std::runtime_error);
}
