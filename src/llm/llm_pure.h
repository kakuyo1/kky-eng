#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <variant>

#include "llm_client.h"   // Config / WordExplanation

/**
 * @file llm_pure.h
 * @brief The network-free core of LlmClient: mask, build the request, check the response.
 *
 * Splitting these out keeps them unit-testable from lens_test --filter, with no socket and
 * no API key. The wire format itself is documented in PHASE1.md section 4.3.
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
 * @brief Build the /chat/completions request body.
 *
 * Centralises the two constraints DeepSeek imposes on JSON output, so a regression cannot
 * quietly drop either one:
 *  - response_format json_object requires the word "json" and a format example in the
 *    prompt, otherwise the model streams whitespace until max_tokens runs out;
 *  - thinking mode is on by default and must be explicitly disabled for this task.
 *
 * @param config          Supplies the model name.
 * @param words           Words to explain; each is masked before it is embedded.
 * @param explanationLang "en" or "zh"; switches the closing sentence of the system prompt.
 * @return A compact JSON body, ready to POST.
 */
QByteArray buildRequestBody(const Config& config, const QStringList& words,
                            const QString& explanationLang);

/**
 * @brief Validate a response that arrives from the network and must not be trusted.
 *
 * Checks, in order: the HTTP envelope is JSON with a `choices` array; finish_reason is
 * `stop`; the message content is JSON; it carries a `results` array; every element has the
 * four required string fields; word/en/zh are non-empty; and the returned words match the
 * requested ones exactly, with no word missing, extra, or misspelled.
 *
 * Any failure rejects the whole batch rather than dropping individual words, because a
 * partial batch would silently explain the wrong thing. The caller sees either the full
 * validated batch or one error message.
 *
 * @param responseBody Raw HTTP response body.
 * @param expectedWords The words the request asked for, in request order.
 * @return The explanations in request order on success, or a user-facing error message
 *         (never containing the API key).
 */
std::variant<QVector<WordExplanation>, QString> parseExplanations(const QByteArray& responseBody,
                                                                  const QStringList& expectedWords);

}
