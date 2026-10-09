#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <filesystem>

/**
 * @file llm_protocol.h
 * @brief The wire protocol as data: one request template and one response schema per
 *        channel, read from `data/llm/` instead of being spelled out in C++.
 *
 * Which channel a request belongs to is decided before anything is sent, and the channels
 * differ in prompt and in response shape, so the protocol is keyed by channel rather than
 * shared. Editing a prompt or adding a response field is a data edit, not a rebuild.
 *
 * The response schema is the single source of truth for the required field list, so the
 * validation cannot drift away from what API.md documents.
 *
 * A channel may expose more than one prompt preset. Sentence has separate translate and explain
 * presets; entity uses one default preset for both action-bar buttons.
 */

namespace lens::llm {

/// Placeholder inside PromptTemplate::systemPromptTemplate that the output-language line
/// is substituted into.
inline constexpr const char* kOutputLanguagePlaceholder = "{outputLanguage}";

/// Placeholders the etymology request is substituted into: the sentence that asks for the word's
/// origin, and the field it adds to the JSON example in the same template. Both belong to the word
/// channel, whose only reader-visible extra is a word's etymology; a template without them is
/// left alone, which is what makes the substitution harmless on the entity and sentence channels.
inline constexpr const char* kEtymologyNotePlaceholder  = "{etymologyNote}";
inline constexpr const char* kEtymologyFieldPlaceholder = "{etymologyField}";

/// @brief Which channel a request belongs to. Decided locally, before anything is sent.
enum class Channel : std::uint8_t { Word = 0,
                                    Entity,
                                    Sentence,
                                    Count };

/// @brief One channel preset's system prompt, from `request.<channel>.json`.
struct PromptTemplate {
    QString systemPromptTemplate;               ///< Carries kOutputLanguagePlaceholder.
    QHash<QString, QString> outputLanguageLine; ///< "en" / "zh" -> closing prompt line.
    /// The sentence asking for the word's origin, substituted into kEtymologyNotePlaceholder --
    /// the reader's setting keeps it or clears it, and one channel prompt therefore serves both.
    QString etymologyNote;
    /// The `"etymology":"..."` fragment for the JSON example at kEtymologyFieldPlaceholder. It
    /// travels with the sentence above: a shape that does not name the field is a field the model
    /// leaves out, and a missing etymology is not something the validator can report.
    QString etymologyField;
};

/// @brief Request half of one channel's protocol, from `request.<channel>.json`.
struct RequestTemplate {
    QHash<QString, PromptTemplate> prompts; ///< Preset name -> system prompt.
    QJsonObject responseFormat;             ///< Passed through as `response_format`.
    QJsonObject thinking;                   ///< Passed through as `thinking`.
    int maxTokens = 0;                      ///< Output token cap.
    bool stream   = false;                  ///< A batch is answered whole.
};

/// @return The lowercase channel name used in file names and log lines, e.g. "word".
/// @throws std::logic_error If @p channel is Channel::Count.
const char* channelKey(Channel channel);

/**
 * @brief Read one channel's `request.<channel>.json` and `response.<channel>.schema.json`.
 *
 * @param channel Channel to load.
 * @param dir Directory holding the files, normally `<repo>/data/llm`.
 * @throws std::runtime_error If a file is missing, is not valid JSON, or leaves a required
 *         value empty. A half-read protocol is a startup fault: quietly defaulting to
 *         built-in values would hide exactly the drift this extraction exists to prevent.
 * @note Call once per channel in use, before the matching accessors.
 */
void loadLlmProtocol(Channel channel, const std::filesystem::path& dir);

/// @return The loaded request template for @p channel.
/// @throws std::logic_error If that channel has not been loaded.
const RequestTemplate& requestTemplate(Channel channel);

/**
 * @brief Return one named prompt preset for a loaded channel.
 * @param channel Channel whose request file was loaded.
 * @param preset Preset name, or "default" for the ordinary channel prompt.
 * @return The prompt and its language-specific closing lines.
 * @throws std::logic_error If the channel or preset has not been loaded.
 */
const PromptTemplate& promptTemplate(Channel channel, const QString& preset = QStringLiteral("default"));

/// @return Field names every element of `results` must carry, read from that channel's
///         response schema `items.required`.
/// @throws std::logic_error If that channel has not been loaded.
const QStringList& requiredResultFields(Channel channel);

/// @return Loaded schema for validating both legacy entries and nested senses.
QJsonObject const& resultSchema(Channel channel);

/// @return Provider and language choices loaded alongside the protocol.
QJsonObject const& serviceCatalog();

/// @return One provider entry, including the optional model-source vendor prefix, or an empty
///         object for an unknown provider.
QJsonObject serviceProvider(QString const& provider);

}
