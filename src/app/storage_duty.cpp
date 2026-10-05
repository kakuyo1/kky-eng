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

void StorageDuty::writeDocument(const char* key, const QVariant& value)
{
    store_.document()[key] = value.toString().toStdString();
    store_.save();
}

void StorageDuty::save() const
{
    store_.save();
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
