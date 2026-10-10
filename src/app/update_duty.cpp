/**
 * @file update_duty.cpp
 * @brief Owns the update check: when it runs, what it says, and what the reader chose.
 */

#include "update_duty.h"

#include <QDesktopServices>
#include <QUrl>

#include <nlohmann/json.hpp>

#include "util/log.h"

namespace lens::app {

UpdateDuty::UpdateDuty(StorageDuty& storage,
                       update::UpdateClient& client,
                       const QString& currentVersion,
                       DateProvider dateProvider,
                       QObject* parent)
    : QObject(parent),
      storage_(storage),
      client_(client),
      dateProvider_(std::move(dateProvider)),
      current_(currentVersion)
{
    connect(&client_, &update::UpdateClient::finished, this, [this](update::Release release) {
        inFlight_ = false;
        applyResult(std::move(release), manual_);
    });
    connect(&client_, &update::UpdateClient::offline, this, [this] {
        inFlight_ = false;
        reportOffline(manual_);
    });
    LENS_INFO("update check ready: current version {}", current_.toStdString());
}

QVariantMap UpdateDuty::update() const
{
    return QVariantMap{{QStringLiteral("state"), state_},
                       {QStringLiteral("current"), current_},
                       {QStringLiteral("latest"), latest_},
                       {QStringLiteral("notes"), notes_},
                       {QStringLiteral("pageUrl"), pageUrl_}};
}

update::StoredUpdate UpdateDuty::stored() const
{
    update::StoredUpdate settings;
    const nlohmann::json& document = storage_.knownStore().document();
    if (not document.is_object() or not document.contains("UPDATE") or not document.at("UPDATE").is_object())
        return settings;

    const nlohmann::json& section = document.at("UPDATE");
    if (section.contains("autoCheck") and section.at("autoCheck").is_boolean())
        settings.autoCheck = section.at("autoCheck").get<bool>();
    if (section.contains("lastCheckDate") and section.at("lastCheckDate").is_string())
        settings.lastCheckDate = QString::fromStdString(section.at("lastCheckDate").get<std::string>());
    if (section.contains("skipped") and section.at("skipped").is_string())
        settings.skipped = QString::fromStdString(section.at("skipped").get<std::string>());
    return settings;
}

void UpdateDuty::writeAutoCheck(bool on)
{
    nlohmann::json& document = storage_.knownStore().document();
    if (not document.is_object())
        return;
    document["UPDATE"]["autoCheck"] = on;
    storage_.save();
}

void UpdateDuty::writeLastCheckDate(const QString& date)
{
    nlohmann::json& document = storage_.knownStore().document();
    if (not document.is_object())
        return;
    document["UPDATE"]["lastCheckDate"] = date.toStdString();
    storage_.save();
}

void UpdateDuty::writeSkipped(const QString& version)
{
    nlohmann::json& document = storage_.knownStore().document();
    if (not document.is_object())
        return;
    document["UPDATE"]["skipped"] = version.toStdString();
    storage_.save();
}

void UpdateDuty::setAutoCheck(bool on)
{
    if (stored().autoCheck == on)
        return;
    writeAutoCheck(on);
    LENS_INFO("the startup update check is now {}", on ? "on" : "off");
    emit updateChanged();
}

bool UpdateDuty::autoCheck() const
{
    return stored().autoCheck;
}

void UpdateDuty::checkAtStartup()
{
    const QString today = dateProvider_().toString(Qt::ISODate);
    if (not update::shouldCheckAutomatically(stored(), today)) {
        LENS_TRACE("the startup update check was not due on {}", today.toStdString());
        return;
    }
    // Written before the answer is known: a machine that cannot reach the source must not
    // retry on every launch, and a failed attempt is still an attempt.
    writeLastCheckDate(today);
    startCheck(false);
}

void UpdateDuty::checkForUpdates()
{
    startCheck(true);
}

void UpdateDuty::startCheck(bool manual)
{
    if (inFlight_) {
        // A reader who pressed the button during the startup check is answered by the answer
        // already on its way, and it is answered as what they asked for: dropping the press
        // would leave the status row saying nothing on a dead network, and would keep the card
        // down for a version they had chosen to skip.
        manual_ = manual_ or manual;
        LENS_INFO("a check is already running; the answer will be reported as {}",
                  manual_ ? "asked for" : "automatic");
        return;
    }
    manual_   = manual;
    inFlight_ = true;
    state_    = QStringLiteral("checking");
    // The card is not taken down for a check the reader asked for. It is what they are reading,
    // and a window that blinks out and comes back in the middle of the screen is a card that
    // moved under their cursor. cardVisible() counts `checking` as still up, and the answer
    // decides: a newer version replaces what is on the card, anything else takes it down.
    emit updateChanged();
    LENS_INFO("checking for updates ({})", manual ? "asked for" : "at startup");
    client_.fetch();
}

void UpdateDuty::applyResult(update::Release const& release, bool manual)
{
    latest_  = release.versionText;
    notes_   = release.notes;
    pageUrl_ = release.pageUrl;

    const auto current   = util::parseTag(current_.toStdString());
    const bool available = current and update::isUpdateAvailable(release.version, *current);
    state_               = available ? QStringLiteral("available") : QStringLiteral("upToDate");

    // The automatic path is the one that may stay quiet, and `skipped` is compared against the
    // exact version -- so a newer release prompts again. The manual path reports whatever is
    // really there, and raises the card when there is something new to look at.
    cardVisible_ = available and (manual or update::shouldPrompt(stored(), latest_, available));

    LENS_INFO("update check answered: latest {} against {} -- {}", latest_.toStdString(), current_.toStdString(), available ? "newer" : "nothing new");
    emit updateChanged();
}

void UpdateDuty::reportOffline(bool manual)
{
    latest_.clear();
    notes_.clear();
    pageUrl_.clear();
    cardVisible_ = false;
    // An automatic check that could not reach the source leaves the state as it was: the reader
    // did not ask, so there is nothing to report. The manual one is the reader asking, and is
    // answered.
    state_ = manual ? QStringLiteral("offline") : QStringLiteral("idle");
    LENS_WARN("the update source could not be reached; {}",
              manual ? "reporting it" : "saying nothing, since nobody asked");
    emit updateChanged();
}

void UpdateDuty::skipUpdate()
{
    if (latest_.isEmpty())
        return;
    writeSkipped(latest_);
    cardVisible_ = false;
    LENS_INFO("skipped version {}", latest_.toStdString());
    emit updateChanged();
}

void UpdateDuty::openReleasePage()
{
    const QUrl url{pageUrl_};
    if (not url.isValid() or url.scheme() != QLatin1String("https") or url.host().isEmpty()) {
        LENS_WARN("the release page is not an https URL with a host; nothing was opened");
        return;
    }
    cardVisible_ = false;
    emit updateChanged();
    LENS_INFO("opening the release page in the browser");
    QDesktopServices::openUrl(url);
}

void UpdateDuty::closeCard()
{
    if (not cardVisible_)
        return;
    cardVisible_ = false;
    emit updateChanged();
}

bool UpdateDuty::cardVisible() const
{
    // `checking` counts as up: a check that is under way does not retract what the card is
    // already saying, and the surfaces take the card down when the answer says otherwise.
    return cardVisible_ and (state_ == QLatin1String("available") or state_ == QLatin1String("checking"));
}

}