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
 * @return A compact JSON body, ready to POST.
 * @throws std::logic_error If that channel's protocol has not been loaded.
 */
QByteArray buildRequestBody(const Config& config,
                            Channel channel,
                            const QStringList& inputs,
                            const QString& explanationLang,
                            const QString& preset = QStringLiteral("default"));

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
 * @brief Read the model ids out of an OpenAI-compatible `/models` response.
 *
 * The service's own answer is the only current list there is: the ids a provider supports change
 * under the app, and a name that is no longer one of them is a 400 (docs/adr/0017). The shape is
 * the one every OpenAI-compatible service returns -- `{"data": [{"id": "..."}, ...]}` -- and
 * anything else is treated as no list at all rather than as a partial one, because a truncated
 * list of models is worse than none: it hides the model the reader wanted.
 *
 * What comes back is the whole catalogue, not the chat models in it: embeddings, speech, image and
 * video models sit in the same answer, and this app can use exactly one kind of them. Those are
 * dropped here, by the response's own modality where the service supplies it and by the id where it
 * does not, so the reader is not handed a thousand names they can never pick.
 *
 * @param body Raw HTTP response body.
 * @return The ids the app can talk to, in the order the service gave them, or an empty list.
 */
QStringList parseModelIds(const QByteArray& body);

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
