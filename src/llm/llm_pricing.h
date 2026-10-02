#pragma once

#include <QHash>
#include <QString>

#include <filesystem>

#include "llm_client.h" // Usage

/**
 * @file llm_pricing.h
 * @brief The price list as data: what a model charges per token, from `data/llm/pricing.json`.
 *
 * The cost surfaces show money, and the service reports only tokens, so the conversion has to
 * happen here. Keeping the numbers in a data file rather than in C++ means a price change is
 * a data edit -- the same reason the prompts and the response schema are data.
 *
 * @note A price list is not a billing statement. See cost() for what it deliberately leaves
 *       out.
 */

namespace lens::llm {

/// @brief What one model charges for each direction of traffic.
struct ModelPrice {
    double input = 0.0;  ///< Per `unit` prompt tokens.
    double output = 0.0; ///< Per `unit` completion tokens.
};

/**
 * @brief The price list, loaded once at startup.
 */
class Pricing {
public:
    /**
     * @brief Read the price list.
     *
     * @param path Normally `<repo>/data/llm/pricing.json`.
     * @throws std::runtime_error If the file is missing, is not valid JSON, states no
     *         currency or unit, or lists no model. A half-read price list is a startup
     *         fault: quietly costing everything at zero would look like the feature working.
     */
    static Pricing load(const std::filesystem::path& path);

    /**
     * @brief What one call cost.
     *
     * @param model Model name as sent on the wire (`Config::model`).
     * @param usage Token counts from that call's response.
     * @return The amount in currency(), or 0 when the model is not in the list, which is
     *         logged. An unlisted model is a price list that needs updating, not a reason to
     *         refuse to show the rest of the tally.
     * @note Two deliberate omissions, both worth more code than they are worth now: the
     *       service quotes peak and off-peak rates and this uses the off-peak one, so a call
     *       made in a peak window is understated by up to 2x; and a prompt whose prefix was
     *       cached is billed far less, which is not distinguishable from the token counts
     *       alone.
     */
    double cost(const QString& model, const Usage& usage) const;

    /// @return The currency the prices are quoted in, e.g. "USD".
    QString currency() const
    {
        return currency_;
    }

private:
    QString currency_ = QStringLiteral("USD");
    long long unit_ = 1000000; ///< Token count the prices are per.
    QHash<QString, ModelPrice> byModel_;
};

}
