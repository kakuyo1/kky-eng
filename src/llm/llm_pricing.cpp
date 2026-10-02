#include "llm_pricing.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <stdexcept>

#include "core/log.h"

namespace lens::llm {

Pricing Pricing::load(const std::filesystem::path& path)
{
    LENS_TRACE("Pricing::load: reading '{}'", path.string());

    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot read the price list: " + path.string());

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        throw std::runtime_error("The price list is not a JSON object: " + path.string());

    const QJsonObject root = doc.object();
    Pricing pricing;
    pricing.currency_ = root.value("currency").toString();
    pricing.unit_ = root.value("unit").toInteger();
    if (pricing.currency_.isEmpty() || pricing.unit_ <= 0)
        throw std::runtime_error("The price list states no currency or no unit: " + path.string());

    const QJsonObject models = root.value("models").toObject();
    for (auto it = models.begin(); it != models.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        const ModelPrice price{entry.value("input").toDouble(), entry.value("output").toDouble()};
        if (price.input <= 0.0 || price.output <= 0.0) {
            LENS_WARN("price list: '{}' has a missing or non-positive rate; ignoring it", it.key().toStdString());
            continue;
        }
        pricing.byModel_.insert(it.key(), price);
    }
    if (pricing.byModel_.isEmpty())
        throw std::runtime_error("The price list names no model: " + path.string());

    LENS_INFO("pricing loaded: {} model(s) in {}", pricing.byModel_.size(), pricing.currency_.toStdString());
    return pricing;
}

double Pricing::cost(const QString& model, const Usage& usage) const
{
    const auto it = byModel_.constFind(model);
    if (it == byModel_.cend()) {
        LENS_WARN("pricing: no rate for model '{}'; this call costs 0 in the tally", model.toStdString());
        return 0.0;
    }

    const double unit = static_cast<double>(unit_);
    return (usage.promptTokens / unit) * it->input + (usage.completionTokens / unit) * it->output;
}

}
