/**
 * @file storage_duty.cpp
 * @brief The single-document storage boundary used by the application duties.
 */

#include "storage_duty.h"

#include <QCoreApplication>

#include <nlohmann/json.hpp>

namespace lens::app {

StorageDuty::StorageDuty(core::KnownStore& store)
    : store_(store), stats_(store.document())
{
    auto& document                    = store_.document();
    const bool removedDragSensitivity = document.erase("dragSensitivity") > 0;
    const bool removedPopupFrequency  = document.erase("popupFrequency") > 0;
    if (removedDragSensitivity or removedPopupFrequency)
        store_.save();
}

core::KnownStore& StorageDuty::knownStore()
{
    return store_;
}

const core::KnownStore& StorageDuty::knownStore() const
{
    return store_;
}

core::StatsStore& StorageDuty::statsStore()
{
    return stats_;
}

const core::StatsStore& StorageDuty::statsStore() const
{
    return stats_;
}

QString StorageDuty::documentString(const char* key, const QString& fallback) const
{
    const nlohmann::json& document = store_.document();
    if (!document.is_object() || !document.contains(key) || !document[key].is_string())
        return fallback;
    return QString::fromStdString(document[key].get<std::string>());
}

bool StorageDuty::documentBool(const char* key, bool fallback) const
{
    const nlohmann::json& document = store_.document();
    if (!document.is_object() || !document.contains(key) || !document[key].is_boolean())
        return fallback;
    return document[key].get<bool>();
}

void StorageDuty::writeDocument(const char* key, const QVariant& value)
{
    const int type = value.typeId();
    if (type == QMetaType::Bool)
        store_.document()[key] = value.toBool();
    else if (type == QMetaType::Int || type == QMetaType::LongLong || type == QMetaType::UInt || type == QMetaType::ULongLong)
        store_.document()[key] = value.toLongLong();
    else if (type == QMetaType::Double || type == QMetaType::Float)
        store_.document()[key] = value.toDouble();
    else
        store_.document()[key] = value.toString().toStdString();
    store_.save();
}

void StorageDuty::save() const
{
    store_.save();
}

bool StorageDuty::removeWord(QString const& lemma)
{
    const auto value = lemma.trimmed().toStdString();
    if (value.empty())
        return false;
    const bool marksRemoved   = store_.removeLemma(value);
    const bool historyRemoved = stats_.removeLemma(value);
    const bool removed        = marksRemoved || historyRemoved;
    if (removed)
        store_.save();
    return removed;
}

QVariantList StorageDuty::levelOptions()
{
    const struct {
        const char* label;
        const char* group;
        const char* note;
    } levels[] = {
        {"B1–B2", QT_TRANSLATE_NOOP("lens::app::AppController", "CEFR"), ""},
        {"C1–C2", QT_TRANSLATE_NOOP("lens::app::AppController", "CEFR"), ""},
        {"CET-4", QT_TRANSLATE_NOOP("lens::app::AppController", "National exams"), "Minimum requirement"},
        {"CET-6", QT_TRANSLATE_NOOP("lens::app::AppController", "National exams"), ""},
        {"TEM-4", QT_TRANSLATE_NOOP("lens::app::AppController", "National exams"), ""},
        {"TEM-8", QT_TRANSLATE_NOOP("lens::app::AppController", "National exams"), ""},
        {"IELTS", QT_TRANSLATE_NOOP("lens::app::AppController", "Study-abroad exams"), ""},
        {"TOEFL", QT_TRANSLATE_NOOP("lens::app::AppController", "Study-abroad exams"), ""},
    };

    QVariantList options;
    int value = 0;
    for (const auto& level : levels) {
        options.append(QVariantMap{{"value", value++},
                                   {"label", QString::fromUtf8(level.label)},
                                   {"group", QCoreApplication::translate("lens::app::AppController", level.group)},
                                   {"note", QString::fromUtf8(level.note)}});
    }
    return options;
}

}
