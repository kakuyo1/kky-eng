#pragma once

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include "core/known_store.h"
#include "core/stats_store.h"

/**
 * @file storage_duty.h
 * @brief The single-document storage boundary used by the application duties.
 */

namespace lens::app {

class StorageDuty final {
public:
    /**
     * @brief Bind the storage boundary to the document owned by @p store.
     * @param store The one owner of the reader's settings document.
     */
    explicit StorageDuty(core::KnownStore& store);

    /// @return The document owner shared by the other duties.
    core::KnownStore& knownStore();
    const core::KnownStore& knownStore() const;

    /// @return The statistics section bound to the same document.
    core::StatsStore& statsStore();
    const core::StatsStore& statsStore() const;

    /// @return A string document value, or @p fallback when it is absent or not a string.
    QString documentString(const char* key, const QString& fallback) const;

    /// @brief Write one string document value and persist the whole document.
    void writeDocument(const char* key, const QVariant& value);

    /// @brief Persist the current document, including statistics changes.
    void save() const;

    /// @return The vocabulary level options shown by the settings surface.
    static QVariantList levelOptions();

private:
    core::KnownStore& store_;
    core::StatsStore stats_;
};

}
