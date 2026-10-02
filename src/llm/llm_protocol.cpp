#include "llm/llm_protocol.h"

#include "core/log.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>

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
    bool loaded = false;
};

std::array<LoadedProtocol, kChannelCount> g_protocols;

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
    return g_protocols[static_cast<std::size_t>(channel)];
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
    const std::string key = channelKey(channel);
    const std::filesystem::path requestFile = dir / ("request." + key + ".json");
    const std::filesystem::path schemaFile = dir / ("response." + key + ".schema.json");

    const QJsonObject root = readJsonObject(requestFile);
    const QJsonObject prompt = root.value("systemPrompt").toObject();

    RequestTemplate request;
    request.systemPromptTemplate = requireString(prompt, "template", requestFile);
    request.responseFormat = root.value("responseFormat").toObject();
    request.thinking = root.value("thinking").toObject();
    request.maxTokens = root.value("maxTokens").toInt();
    request.stream = root.value("stream").toBool();

    const QJsonObject lines = prompt.value("outputLanguage").toObject();
    for (auto it = lines.begin(); it != lines.end(); ++it)
        request.outputLanguageLine.insert(it.key(), it.value().toString());

    if (!request.systemPromptTemplate.contains(QLatin1String(kOutputLanguagePlaceholder)))
        throw std::runtime_error(std::string("prompt template has no ") + kOutputLanguagePlaceholder +
                                 " placeholder in " + requestFile.string());
    if (!request.outputLanguageLine.contains(QStringLiteral("en")))
        throw std::runtime_error("missing 'systemPrompt.outputLanguage.en' in " + requestFile.string());
    if (!request.outputLanguageLine.contains(QStringLiteral("zh")))
        throw std::runtime_error("missing 'systemPrompt.outputLanguage.zh' in " + requestFile.string());
    if (request.maxTokens <= 0)
        throw std::runtime_error("'maxTokens' must be positive in " + requestFile.string());
    requireNonEmpty(request.responseFormat, "responseFormat", requestFile);
    requireNonEmpty(request.thinking, "thinking", requestFile);

    const QJsonObject schema = readJsonObject(schemaFile);
    const QJsonObject items = schema.value(QStringLiteral("properties"))
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

    LoadedProtocol& slot = slotFor(channel);
    slot.request = std::move(request);
    slot.requiredFields = std::move(required);
    slot.loaded = true;

    LENS_INFO("llm protocol loaded for channel '{}': {} required result field(s), max_tokens={}", key, slot.requiredFields.size(), slot.request.maxTokens);
    LENS_TRACE("channel '{}' system prompt is {} chars", key, slot.request.systemPromptTemplate.size());
}

const RequestTemplate& requestTemplate(Channel channel)
{
    const LoadedProtocol& slot = slotFor(channel);
    if (!slot.loaded)
        throw std::logic_error(std::string("lens::llm::requestTemplate: channel '") + channelKey(channel) +
                               "' has not been loaded");
    return slot.request;
}

const QStringList& requiredResultFields(Channel channel)
{
    const LoadedProtocol& slot = slotFor(channel);
    if (!slot.loaded)
        throw std::logic_error(std::string("lens::llm::requiredResultFields: channel '") +
                               channelKey(channel) + "' has not been loaded");
    return slot.requiredFields;
}

}
