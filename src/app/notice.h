#pragma once

#include <QString>
#include <QVariantMap>

/**
 * @file notice.h
 * @brief The payload a notice surface binds to: the reason a pop had nothing to explain.
 *
 * A notice is not an explanation and carries no verdict. It exists so the bubble stops being
 * the error reporter: a request that failed, and a selection with no word to look up, both
 * arrive here instead, and the surface that draws a notice can be built on its own.
 *
 * The shape lives here rather than in AppController so that the surface, the controller and
 * the offline test all read one definition of it.
 */

namespace lens::app {

/// @brief Kinds a notice carries. Nothing depends on a particular rendering of them yet; the
///        surface that arrives later decides whether it distinguishes them.
inline constexpr const char* kNoticeInfo  = "info";
inline constexpr const char* kNoticeError = "error";

/**
 * @brief The map a notice surface reads.
 * @param title  What the notice is about -- normally the selection it answers.
 * @param body   Reader-facing reason, already translated.
 * @param kind   kNoticeInfo or kNoticeError.
 * @param action Label for the one thing the reader can do about it, empty when there is none.
 * @param lemma  The word that action would explain, when the notice is about a word.
 * @return {title, body, kind, action, lemma}. An empty map is not a notice: it means there is none.
 */
inline QVariantMap noticePayload(const QString& title, const QString& body, const QString& kind, const QString& action = {}, const QString& lemma = {})
{
    return QVariantMap{{"title", title}, {"body", body}, {"kind", kind}, {"action", action}, {"lemma", lemma}};
}

}
