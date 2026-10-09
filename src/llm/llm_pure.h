#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <variant>

#include "llm_client.h"   // Config / Explanation
#include "llm_protocol.h" // Channel

/**
 * @file llm_pure.h
 * @brief The network-free core of LlmClient: mask, build the request, check the response.
 *
 * The prompt, the envelope defaults, and the response schema all come from `data/llm/`
 * (see llm_protocol.h); what lives here is the assembly and the checks. That keeps this
 * unit-testable from the lens_gtest_unit target, with no socket and no API key.
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
 * @brief Map an HTTP status onto something the reader can act on.
 *
 * Lives here rather than beside the transport so it can be tested without a network, the same
 * reason the request body and the response check do. The API key is never part of the message,
 * and neither is the response body, which may echo the request.
 *
 * @param status HTTP status code the service answered with.
 * @return A reader-facing reason, already routed through translation.
 */
QString httpErrorFor(int status);

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
 * @param inputs          Payload to explain; each entry is masked before it is embedded.
 * @param explanationLang "en" or "zh"; picks the closing line of the system prompt.
 *                        An unknown value falls back to "en" and logs a warning.
 * @param preset          Prompt preset, such as sentence's "translate" or "explain".
 * @param etymology       Ask for the word's origin as well. The word channel's templates carry the
 *                        placeholders for it; a channel without them ignores the flag. Costs output
 *                        tokens, so it is the reader's setting and never this function's default.
 * @return A compact JSON body, ready to POST.
 * @throws std::logic_error If that channel's protocol has not been loaded.
 */
QByteArray buildRequestBody(const Config& config,
                            Channel channel,
                            const QStringList& inputs,
                            const QString& explanationLang,
                            const QString& preset = QStringLiteral("default"),
                            bool etymology        = false);

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
 * @param expectedInputs The payload the request asked for, in request order.
 * @return The explanations in request order on success, or a reader-facing message
 *         (never containing the API key, and marked for translation).
 * @throws std::logic_error If that channel's protocol has not been loaded.
 */
std::variant<QVector<Explanation>, QString> parseExplanations(Channel channel,
                                                              const QByteArray& responseBody,
                                                              const QStringList& expectedInputs,
                                                              QString const& explanationLang = QStringLiteral("en"),
                                                              bool multipleSenses            = false);

/**
 * @brief Read the ids out of OpenRouter's model answer.
 *
 * One source, one envelope: there is no mapping parameter any more, because there is only one
 * endpoint left to read. Every id the answer carries for a modality other than text, and every
 * routing variant (an id carrying `:`), is dropped -- what comes back is not the catalogue but
 * the part of it a chat completion can be sent to.
 *
 * A malformed answer is no answer: missing, non-array, oversized or otherwise unusable input
 * returns an empty list rather than a partial one, so a reader is never handed a list that hides
 * the model they wanted.
 *
 * @param body Raw HTTP response body.
 * @return The ids this app can talk to, in the order the source gave them, or an empty list.
 */
QStringList parseModelIds(QByteArray const& body);

/**
 * @brief The ids one OpenRouter vendor prefix names, with that prefix removed.
 *
 * An id is matched on `prefix + "/"` exactly, so `deepseek-x/gpt` is not a `deepseek` id.
 *
 * @param ids    The whole catalogue, as parseModelIds() returned it.
 * @param prefix Vendor part of an OpenRouter id, such as "deepseek". An empty prefix returns
 *               @p ids unchanged: that is OpenRouter's own list, where the prefix is part of
 *               the id the API takes.
 * @return The matching ids, in the order @p ids gave them.
 */
QStringList idsForPrefix(QStringList const& ids, QString const& prefix);

/**
 * @brief Read the token counts out of a response envelope.
 *
 * `usage` sits on the envelope, not in the payload, so it is outside everything
 * parseExplanations() checks: a response with perfect explanations and no `usage` is still a
 * success, and one with `usage` and a malformed payload still fails. Losing a line of the
 * cost tally is survivable; dropping an explanation that arrived intact is not.
 *
 * @param responseBody Raw HTTP response body.
 * @return The counts, or zero for any field the response omits or states as a non-number.
 */
Usage parseUsage(const QByteArray& responseBody);

}
