/**
 * @file capture_duty.cpp
 * @brief Selection capture and classification, independent of explanation and presentation.
 */

#include "capture_duty.h"

#include <algorithm>
#include <iterator>

#include "core/filter_core.h"
#include "core/log.h"
#include "mouse_selection_hook.h"

namespace lens::app {
namespace {

constexpr int kMaxSelectionChars = 1000;

std::size_t rankForLevel(int level)
{
    static constexpr std::size_t kRanks[] = {4000, 9000, 5000, 8000, 8000, 12000, 9000, 12000};
    constexpr int kLast                   = static_cast<int>(std::size(kRanks)) - 1;
    return kRanks[std::clamp(level, 0, kLast)];
}

}

CaptureDuty::CaptureDuty(StorageDuty& storage, MouseSelectionHook& hook, QObject* parent)
    : QObject(parent), storage_(storage), hook_(hook), grabber_(std::make_unique<SelectionTextGrabber>())
{
    connect(&hook_, &MouseSelectionHook::selectionReleased, this, &CaptureDuty::onSelectionReleased);
    connect(&hook_, &MouseSelectionHook::pointerPressed, this, &CaptureDuty::pointerPressed);
}

void CaptureDuty::onSelectionReleased(QPoint anchor)
{
    if (storage_.documentString("selectionCapture", QStringLiteral("true")) != QLatin1String("true")) {
        LENS_TRACE("selection at ({}, {}) ignored: selection capture is off", anchor.x(), anchor.y());
        return;
    }
    beginSelection(anchor);
}

void CaptureDuty::beginSelection(QPoint anchor)
{
    const auto grabbed = grabber_->grab();
    if (const auto* status = std::get_if<GrabStatus>(&grabbed)) {
        LENS_DEBUG("selection at ({}, {}) produced no text (status {})", anchor.x(), anchor.y(), static_cast<int>(*status));
        return;
    }

    const GrabbedText& captured = std::get<GrabbedText>(grabbed);
    if (captured.clipboardReplaced &&
        storage_.documentString("clipboardPolicy", QStringLiteral("topmost")) == QLatin1String("silent")) {
        LENS_INFO("the clipboard was rewritten during the grab; the policy is silent, so the selection is dropped");
        return;
    }

    QString text = captured.text;
    if (text.size() > kMaxSelectionChars)
        text = text.left(kMaxSelectionChars) + QStringLiteral("…");

    const core::Selection selection = core::classifySelection(text.toStdString(),
                                                              storage_.knownStore().known(),
                                                              minFreqRank());
    PendingSelection pending{.anchor = anchor, .text = text};
    switch (selection.kind) {
        case core::SelectionKind::Entity:
            pending.kind = QStringLiteral("entity");
            LENS_INFO("selection of {} character(s): entity channel", text.size());
            break;
        case core::SelectionKind::Sentence:
            pending.kind = QStringLiteral("sentence");
            LENS_INFO("selection of {} character(s): sentence channel", text.size());
            break;
        case core::SelectionKind::Word: {
            const auto& candidates      = selection.candidates;
            const auto fresh            = std::find_if(candidates.begin(), candidates.end(), [](const core::Candidate& candidate) {
                return candidate.state == core::CandidateState::New;
            });
            const core::Candidate& word = fresh != candidates.end() ? *fresh : candidates.front();
            pending.kind                = QStringLiteral("word");
            pending.surface             = QString::fromStdString(word.surface);
            pending.lemma               = QString::fromStdString(word.lemma);
            LENS_INFO("selection of {} character(s): word channel, '{}' -> '{}'", text.size(), pending.surface.toStdString(), pending.lemma.toStdString());
            break;
        }
    }

    emit selectionReady(pending);
    emit selectionBarRequested(QVariantMap{{"x", anchor.x()},
                                           {"y", anchor.y()},
                                           {"kind", pending.kind},
                                           {"text", pending.text}});
}

std::size_t CaptureDuty::minFreqRank() const
{
    return rankForLevel(storage_.knownStore().level());
}

}
