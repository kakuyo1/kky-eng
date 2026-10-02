#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <variant>

#include "llm_client.h"    // Config / WordExplanation
#include "llm_protocol.h"  // Channel

/**
 * @file llm_pure.h
 * @brief The network-free core of LlmClient: mask, build the request, check the response.
 *
 * The prompt, the envelope defaults, and the response schema all come from `data/llm/`
 * (see llm_protocol.h); what lives here is the assembly and the checks. That keeps this
 * unit-testable from lens_test --filter, with no socket and no API key.
 */

namespace lens::llm {

/**
 * @brief Mask anything sensitive that slipped through before a request leaves the machine.
 *
 * Email addresses, URLs, and runs of 6+ digits each collapse to a placeholder. Words have
 * already been hard-filtered by FilterCore, so this is a backstop rather than the main
 * defence.
 *
 * @param text Text about to be sent.
 * @return The text with matches replaced by `<email>`, `<url>`, or `<num>`.
 */
QString maskSensitive(const QString& text);

/**
 * @brief Build the /chat/completions request body for one channel.
 *
 * Channel decides which template is used, so the caller must know whether it is sending a
 * word, an entity, or a sentence before it gets here. The envelope defaults
 * (`response_format`, `thinking`, `max_tokens`, `stream`) come from that channel's
 * `request.<channel>.json`, as does the system prompt.
 *
 * @param config          Supplies the model name.
 * @param channel         Which protocol to speak.
 * @param words           Payload to explain; each entry is masked before it is embedded.
 * @param explanationLang "en" or "zh"; picks the closing line of the system prompt.
 *                        An unknown value falls back to "en" and logs a warning.
 * @return A compact JSON body, ready to POST.
 * @throws std::logic_error If that channel's protocol has not been loaded.
 */
QByteArray buildRequestBody(const Config& config, Channel channel, const QStringList& words,
                            const QString& explanationLang);

/**
 * @brief Validate a response that arrives from the network and must not be trusted.
 *
 * Checks, in order: the HTTP envelope is JSON with a `choices` array; finish_reason is
 * `stop`; the message content is JSON; it carries a `results` array; every element is an
 * object holding every field that channel's response schema marks required; the fields the
 * overlay shows are non-empty; and the returned words match the requested ones exactly,
 * with no word missing, extra, or misspelled.
 *
 * Any failure rejects the whole batch rather than dropping individual words, because a
 * partial batch would silently explain the wrong thing. The caller sees either the full
 * validated batch or one message.
 *
 * @param channel       Which protocol was spoken; selects the response schema.
 * @param responseBody  Raw HTTP response body.
 * @param expectedWords The payload the request asked for, in request order.
 * @return The explanations in request order on success, or a reader-facing message
 *         (never containing the API key, and marked for translation).
 * @throws std::logic_error If that channel's protocol has not been loaded.
 */
std::variant<QVector<WordExplanation>, QString> parseExplanations(Channel channel,
                                                                  const QByteArray& responseBody,
                                                                  const QStringList& expectedWords);

}
