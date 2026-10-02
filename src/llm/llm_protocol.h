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
 * validation cannot drift away from what LLM.md documents.
 *
 * @note Only the word channel is specified today; the entity and sentence channels are
 *       phase-2 placeholders (PHASE1.md section 2). Adding one means dropping
 *       `request.<channel>.json` and `response.<channel>.schema.json` into the directory.
 */

namespace lens::llm {

/// Placeholder inside RequestTemplate::systemPromptTemplate that the output-language line
/// is substituted into.
inline constexpr const char* kOutputLanguagePlaceholder = "{outputLanguage}";

/// @brief Which channel a request belongs to. Decided locally, before anything is sent.
enum class Channel : std::uint8_t { Word = 0, Entity, Sentence, Count };

/// @brief Request half of one channel's protocol, from `request.<channel>.json`.
struct RequestTemplate {
    QString systemPromptTemplate;                ///< Carries kOutputLanguagePlaceholder.
    QHash<QString, QString> outputLanguageLine;  ///< "en" / "zh" -> closing prompt line.
    QJsonObject responseFormat;                  ///< Passed through as `response_format`.
    QJsonObject thinking;                        ///< Passed through as `thinking`.
    int maxTokens = 0;                           ///< Output token cap.
    bool stream = false;                         ///< A batch is answered whole.
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

/// @return Field names every element of `results` must carry, read from that channel's
///         response schema `items.required`.
/// @throws std::logic_error If that channel has not been loaded.
const QStringList& requiredResultFields(Channel channel);

}
