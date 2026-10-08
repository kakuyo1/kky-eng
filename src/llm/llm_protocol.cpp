#include "llm/llm_protocol.h"

#include "core/log.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace lens::llm {
namespace {

constexpr std::size_t kChannelCount = static_cast<std::size_t>(Channel::Count);

/// @brief One channel's loaded protocol. `loaded` separates "not read yet" from "read and
///        turned out empty", which would otherwise both look like an absent template.
struct LoadedProtocol {
    RequestTemplate request;
    QStringList requiredFields;
    QJsonObject schema;
    bool loaded = false;
};

std::array<LoadedProtocol, kChannelCount> g_protocols;
QJsonObject g_catalog;

/// @brief Read a file as raw bytes.
/// @note std::ifstream rather than QFile: it takes the path natively, so a non-ASCII
///       directory never has to survive a narrow-string round trip.
QByteArray readFile(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + file.string());

    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    return QByteArray(text.data(), static_cast<qsizetype>(text.size()));
}

QJsonObject readJsonObject(const std::filesystem::path& file)
{
    const auto parsed = QJsonDocument::fromJson(readFile(file));
    if (!parsed.isObject()) throw std::runtime_error("not a JSON object: " + file.string());
    return parsed.object();
}

/// @throws std::runtime_error Naming the key and the file, so a hand-edited protocol file
///         fails with something actionable instead of a default value.
QString requireString(const QJsonObject& object, const char* key, const std::filesystem::path& file)
{
    const QString value = object.value(QLatin1String(key)).toString();
    if (value.isEmpty())
        throw std::runtime_error(std::string("missing or empty '") + key + "' in " + file.string());
    return value;
}

void requireNonEmpty(const QJsonObject& value, const char* key, const std::filesystem::path& file)
{
    if (value.isEmpty())
        throw std::runtime_error(std::string("missing or empty '") + key + "' in " + file.string());
}

LoadedProtocol& slotFor(Channel channel)
{
    channelKey(channel);
    return g_protocols[static_cast<std::size_t>(channel)];
}

/// @param etymology The channel's `etymologyPrompt` block, passed in rather than read from
///        @p preset: the sentence is a property of the channel, and a preset written as its own
///        object -- `multiplePrompt`, or an entry of `presets` -- carries only its own prompt.
PromptTemplate loadPrompt(const QJsonObject& preset, const std::filesystem::path& file, const QJsonObject& etymology)
{
    const QJsonObject prompt = preset.value("systemPrompt").toObject();

    PromptTemplate loaded;
    loaded.systemPromptTemplate = requireString(prompt, "template", file);
    const QJsonObject lines     = prompt.value("outputLanguage").toObject();
    for (auto it = lines.begin(); it != lines.end(); ++it) {
        if (not it.value().isString() or it.value().toString().trimmed().isEmpty())
            throw std::runtime_error("empty output language in " + file.string());
        loaded.outputLanguageLine.insert(it.key(), it.value().toString());
    }

    if (!loaded.systemPromptTemplate.contains(QLatin1String(kOutputLanguagePlaceholder)))
        throw std::runtime_error(std::string("prompt template has no ") + kOutputLanguagePlaceholder +
                                 " placeholder in " + file.string());
    if (!loaded.outputLanguageLine.contains(QStringLiteral("en")))
        throw std::runtime_error("missing 'systemPrompt.outputLanguage.en' in " + file.string());
    if (!loaded.outputLanguageLine.contains(QStringLiteral("zh")))
        throw std::runtime_error("missing 'systemPrompt.outputLanguage.zh' in " + file.string());

    // A template that asks for the origin but has nothing to substitute is a half-edited protocol
    // file, and its symptom -- a bubble with no etymology -- names neither the file nor the field,
    // so it throws here instead.
    loaded.etymologyNote     = etymology.value("note").toString();
    loaded.etymologyField    = etymology.value("field").toString();
    const bool asksEtymology = loaded.systemPromptTemplate.contains(QLatin1String(kEtymologyNotePlaceholder)) or
                               loaded.systemPromptTemplate.contains(QLatin1String(kEtymologyFieldPlaceholder));
    if (asksEtymology and (loaded.etymologyNote.isEmpty() or loaded.etymologyField.isEmpty()))
        throw std::runtime_error("prompt template asks for an etymology but 'etymologyPrompt' is missing or empty in " +
                                 file.string());
    return loaded;
}

} // namespace

const char* channelKey(Channel channel)
{
    switch (channel) {
        case Channel::Word: return "word";
        case Channel::Entity: return "entity";
        case Channel::Sentence: return "sentence";
        case Channel::Count: break;
    }
    throw std::logic_error("lens::llm::channelKey: Channel::Count is not a real channel");
}

void loadLlmProtocol(Channel channel, const std::filesystem::path& dir)
{
    const std::string key                   = channelKey(channel);
    const std::filesystem::path requestFile = dir / ("request." + key + ".json");
    const std::filesystem::path schemaFile  = dir / ("response." + key + ".schema.json");

    const QJsonObject root = readJsonObject(requestFile);
    RequestTemplate request;
    request.responseFormat = root.value("responseFormat").toObject();
    request.thinking       = root.value("thinking").toObject();
    request.maxTokens      = root.value("maxTokens").toInt();
    request.stream         = root.value("stream").toBool();

    // Read once, at the channel's own level: both the ordinary prompt and the multiple-senses one
    // can be asked for the same extra field, and one copy of the sentence is what keeps them saying
    // the same thing.
    const QJsonObject etymology = root.value("etymologyPrompt").toObject();

    const QJsonObject presets = root.value("presets").toObject();
    if (presets.isEmpty()) {
        request.prompts.insert(QStringLiteral("default"), loadPrompt(root, requestFile, etymology));
    } else {
        for (auto it = presets.begin(); it != presets.end(); ++it) {
            if (!it.value().isObject())
                throw std::runtime_error("preset '" + it.key().toStdString() + "' is not an object in " + requestFile.string());
            request.prompts.insert(it.key(), loadPrompt(it.value().toObject(), requestFile, etymology));
        }
    }
    if (request.prompts.isEmpty())
        throw std::runtime_error("no prompt presets in " + requestFile.string());
    if (root.contains("multiplePrompt"))
        request.prompts.insert(QStringLiteral("multiple"), loadPrompt(root.value("multiplePrompt").toObject(), requestFile, etymology));
    if (request.maxTokens <= 0)
        throw std::runtime_error("'maxTokens' must be positive in " + requestFile.string());
    requireNonEmpty(request.responseFormat, "responseFormat", requestFile);

    const QJsonObject schema = readJsonObject(schemaFile);
    const QJsonObject items  = schema.value(QStringLiteral("properties"))
                                  .toObject()
                                  .value(QStringLiteral("results"))
                                  .toObject()
                                  .value(QStringLiteral("items"))
                                  .toObject();
    QStringList required;
    for (const auto& entry : items.value(QStringLiteral("required")).toArray()) {
        const QString name = entry.toString();
        if (!name.isEmpty()) required << name;
    }
    if (required.isEmpty())
        throw std::runtime_error("no 'properties.results.items.required' in " + schemaFile.string());

    auto catalog         = readJsonObject(dir / "catalog.json");
    auto const providers = catalog.value("providers").toArray();
    auto const languages = catalog.value("languages").toArray();
    if (providers.isEmpty() or languages.isEmpty())
        throw std::runtime_error("empty provider or language catalog");
    auto const defaultProvider = requireString(catalog, "defaultProvider", dir / "catalog.json");
    bool hasDefault            = false;
    for (auto const& value : languages) {
        auto const language = value.toObject();
        auto const code     = requireString(language, "value", dir / "catalog.json");
        requireString(language, "label", dir / "catalog.json");
        for (auto const& prompt : request.prompts)
            if (not prompt.outputLanguageLine.contains(code))
                throw std::runtime_error("catalog language missing from prompt: " + code.toStdString());
    }
    for (auto const& value : providers) {
        auto const provider = value.toObject();
        auto const name     = requireString(provider, "value", dir / "catalog.json");
        hasDefault          = hasDefault or name == defaultProvider;
        requireString(provider, "label", dir / "catalog.json");
        requireString(provider, "group", dir / "catalog.json");
        if (name == QLatin1String("custom")) continue;
        auto const url = QUrl{requireString(provider, "baseUrl", dir / "catalog.json")};
        if (not url.isValid() or url.scheme() != QLatin1String("https") or url.host().isEmpty() or not url.userInfo().isEmpty() or url.hasQuery() or url.hasFragment())
            throw std::runtime_error("invalid provider base URL in catalog");
        const auto modelList = provider.value("modelList").toObject();
        if (requireString(modelList, "method", dir / "catalog.json") != QLatin1String("GET"))
            throw std::runtime_error("unsupported model-list method in catalog");
        const auto modelPath = QUrl{requireString(modelList, "path", dir / "catalog.json")};
        if (modelPath.isRelative() and not modelPath.path().startsWith(QLatin1Char('/')))
            throw std::runtime_error("relative model-list path must start with '/' in catalog");
        if (not modelPath.isRelative() and (not modelPath.isValid() or modelPath.scheme() != QLatin1String("https") or
                                            modelPath.host().isEmpty() or not modelPath.userInfo().isEmpty() or
                                            modelPath.hasQuery() or modelPath.hasFragment() or modelPath.host() != url.host()))
            throw std::runtime_error("invalid absolute model-list URL in catalog");
        const auto auth       = modelList.value("auth").toObject();
        const auto authType   = requireString(auth, "type", dir / "catalog.json");
        const auto authHeader = requireString(auth, "header", dir / "catalog.json");
        if (not((authType == QLatin1String("bearer") and authHeader == QLatin1String("Authorization")) or
                (authType == QLatin1String("header") and authHeader == QLatin1String("x-goog-api-key"))))
            throw std::runtime_error("unsupported model-list authentication in catalog");
        const auto response = modelList.value("response").toObject();
        requireString(response, "arrayField", dir / "catalog.json");
        requireString(response, "idField", dir / "catalog.json");
        requireString(modelList, "docs", dir / "catalog.json");
    }
    if (not hasDefault) throw std::runtime_error("default provider is absent from catalog");
    try {
        catalog.insert("pricing", readJsonObject(dir / "pricing.json"));
    } catch (std::runtime_error const&) {
        LENS_WARN("catalog: price list unavailable; model selection has no listed prices");
    }
    LoadedProtocol& slot = slotFor(channel);
    slot.request         = std::move(request);
    slot.requiredFields  = std::move(required);
    slot.schema          = items;
    slot.loaded          = true;
    g_catalog            = std::move(catalog);

    LENS_INFO("llm protocol loaded for channel '{}': {} preset(s), {} required result field(s), max_tokens={}", key, slot.request.prompts.size(), slot.requiredFields.size(), slot.request.maxTokens);
}

const RequestTemplate& requestTemplate(Channel channel)
{
    const LoadedProtocol& slot = slotFor(channel);
    if (!slot.loaded)
        throw std::logic_error(std::string("lens::llm::requestTemplate: channel '") + channelKey(channel) +
                               "' has not been loaded");
    return slot.request;
}

const PromptTemplate& promptTemplate(Channel channel, const QString& preset)
{
    const RequestTemplate& request = requestTemplate(channel);
    const auto it                  = request.prompts.constFind(preset);
    if (it == request.prompts.cend())
        throw std::logic_error("lens::llm::promptTemplate: preset '" + preset.toStdString() +
                               "' is not loaded for channel '" + channelKey(channel) + "'");
    return it.value();
}

const QStringList& requiredResultFields(Channel channel)
{
    const LoadedProtocol& slot = slotFor(channel);
    if (!slot.loaded)
        throw std::logic_error(std::string("lens::llm::requiredResultFields: channel '") +
                               channelKey(channel) + "' has not been loaded");
    return slot.requiredFields;
}

QJsonObject const& resultSchema(Channel const channel)
{
    requestTemplate(channel);
    return slotFor(channel).schema;
}

QJsonObject const& serviceCatalog()
{
    if (g_catalog.isEmpty()) throw std::logic_error("LLM catalog has not been loaded");
    return g_catalog;
}

QJsonObject serviceProvider(QString const& provider)
{
    for (auto const& value : serviceCatalog().value("providers").toArray()) {
        auto const entry = value.toObject();
        if (entry.value("value").toString() == provider) return entry;
    }
    return {};
}

}
