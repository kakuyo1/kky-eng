/**
 * @file pet_controller.cpp
 * @brief Implementation of PetController: the frame clock, the blink, and what the scene reads.
 */

#include "app/pet/pet_controller.h"

#include <algorithm>
#include <random>
#include <utility>

#include <QCursor>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QScreen>
#include <QUrl>
#include <QWindow>

#include "app/pet/pet_placement.h"
#include "app/pet/pet_window_native.h"
#include "util/log.h"

namespace lens::app::pet {

PetController* PetController::instance_ = nullptr;

namespace {

using core::pet::PetEvent;
using core::pet::Slot;

/// One tick of the single frame clock. Frames are at most 12 fps, so a tick of this length keeps them within one step.
constexpr std::chrono::milliseconds kTick{20};
/// Time between blinks while the dog is awake. The prototype blinks on the same cadence.
constexpr std::chrono::milliseconds kBlinkEvery{4000};

QPoint toPoint(core::pet::Point const& point)
{
    return QPoint{point.x, point.y};
}

/// @return The point an accessory in `slot` is drawn at, for one frame's anchors.
QPoint pointFor(core::pet::Anchors const& anchors, Slot slot)
{
    switch (slot) {
        case Slot::Head:
            return toPoint(anchors.head);
        case Slot::Face:
            return toPoint(anchors.face);
        case Slot::Body:
            return toPoint(anchors.body);
    }
    return {};
}

/// @return The file as a URL string, which is what an Image source takes. A Windows path would read as a scheme.
QString urlOf(QString const& path)
{
    return QUrl::fromLocalFile(path).toString();
}

/// @return What the store should hold for the accessories the wardrobe is wearing.
Outfit outfitOf(core::pet::Wardrobe const& wardrobe)
{
    return Outfit{wardrobe.wornIn(Slot::Head), wardrobe.wornIn(Slot::Face), wardrobe.wornIn(Slot::Body)};
}

/// @return The slot's name as the data and the settings page spell it.
QString slotName(Slot slot)
{
    switch (slot) {
        case Slot::Head:
            return QStringLiteral("head");
        case Slot::Face:
            return QStringLiteral("face");
        case Slot::Body:
            return QStringLiteral("body");
    }
    return {};
}

} // namespace

PetController::PetController(std::optional<PetAssets> assets, PetStore& store, QObject* parent)
    : QObject(parent), assets_(std::move(assets)), store_(store)
{
    clock_.setInterval(kTick);
    connect(&clock_, &QTimer::timeout, this, &PetController::tick);
    if (not assets_) return;

    auto generator = std::mt19937{std::random_device{}()};
    machine_       = std::make_unique<core::pet::PetStateMachine>(
        assets_->animations.actions,
        [generator = std::move(generator)]() mutable { return std::uniform_real_distribution<double>{0.0, 1.0}(generator); });
    wardrobe_ = std::make_unique<core::pet::Wardrobe>(assets_->catalogue);

    auto const outfit = store_.outfit();
    for (auto const* slot : {&outfit.head, &outfit.face, &outfit.body}) {
        if (*slot and knownAccessory(QString::fromStdString(**slot))) wardrobe_->toggle(**slot);
    }
    blinkWait_ = kBlinkEvery;
    sync(std::chrono::milliseconds{0});
}

PetController::~PetController() = default;

void PetController::provide(PetController* instance)
{
    instance_ = instance;
}

PetController* PetController::create(QQmlEngine* engine, QJSEngine* scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    Q_ASSERT(instance_ != nullptr);
    QQmlEngine::setObjectOwnership(instance_, QQmlEngine::CppOwnership);
    return instance_;
}

void PetController::handle(PetEvent event)
{
    if (not machine_) return;
    machine_->handle(event);
    sync(std::chrono::milliseconds{0});
}

bool PetController::available() const
{
    return assets_.has_value();
}

bool PetController::enabled() const
{
    return available() and store_.enabled();
}

bool PetController::passthrough() const
{
    return store_.passthrough();
}

QString PetController::action() const
{
    return action_;
}

int PetController::frameIndex() const
{
    return frameIndex_;
}

int PetController::expression() const
{
    return expression_;
}

QVariantMap PetController::sheets() const
{
    return sheets_;
}

QVariantMap PetController::anchors() const
{
    return QVariantMap{{"body", toPoint(anchors_.body)}, {"face", toPoint(anchors_.face)}};
}

QVariantList PetController::wornAccessories() const
{
    QVariantList worn;
    if (not wardrobe_) return worn;

    auto const& catalogue = assets_->catalogue;
    for (auto const& id : wardrobe_->visibleFor(shownAction())) {
        auto const spec = std::find_if(catalogue.begin(), catalogue.end(), [&id](auto const& candidate) { return candidate.id == id; });
        if (spec == catalogue.end()) continue;
        auto const& art = assets_->accessories.at(id);
        auto const at   = pointFor(anchors_, spec->slot);
        worn.append(QVariantMap{{"asset", urlOf(art.asset)}, {"zIndex", art.zIndex}, {"x", at.x()}, {"y", at.y()}});
    }
    return worn;
}

QStringList PetController::actions() const
{
    auto names = QStringList{};
    if (not assets_) return names;
    for (auto const& entry : assets_->animations.actions)
        names.append(QString::fromStdString(actionName(entry.first)));
    return names;
}

QVariantList PetController::accessories() const
{
    auto list = QVariantList{};
    if (not assets_) return list;
    for (auto const& spec : assets_->catalogue) {
        list.append(QVariantMap{{"id", QString::fromStdString(spec.id)}, {"slot", slotName(spec.slot)}});
    }
    return list;
}

QVariantMap PetController::wornIn() const
{
    auto worn = QVariantMap{};
    if (not wardrobe_) return worn;
    for (auto const slot : {Slot::Head, Slot::Face, Slot::Body}) {
        worn.insert(slotName(slot), QString::fromStdString(wardrobe_->wornIn(slot).value_or(std::string{})));
    }
    return worn;
}

QVariantMap PetController::layers() const
{
    if (not assets_) return {};
    return QVariantMap{{"body", assets_->layers.body}, {"expression", assets_->layers.expression}, {"effect", assets_->layers.effect}};
}

int PetController::canvas() const
{
    return assets_ ? assets_->canvas : 0;
}

int PetController::scale() const
{
    return store_.scale(assets_ ? assets_->scale : kMinScale);
}

int PetController::minScale() const
{
    return kMinScale;
}

int PetController::maxScale() const
{
    return kMaxScale;
}

void PetController::setScale(int times)
{
    store_.setScale(times);
    emit settingsChanged();
}

void PetController::setRunning(bool on)
{
    desktopRunning_ = on;
    updateClock();
}

void PetController::setPreviewing(bool on)
{
    previewing_ = on;
    if (not on and preview_) {
        preview_.reset();
        sync(std::chrono::milliseconds{0});
    }
    updateClock();
}

void PetController::preview(QString action)
{
    auto const picked = actionNamed(action);
    if (not picked) return;
    // From the first frame even when it is the action already shown, so a second press plays it again.
    preview_       = picked;
    actionElapsed_ = std::chrono::milliseconds{0};
    sync(std::chrono::milliseconds{0});
}

void PetController::dragStart()
{
    handle(PetEvent::DragStart);
}

void PetController::dragEnd()
{
    handle(PetEvent::DragEnd);
}

void PetController::click()
{
    handle(PetEvent::Click);
}

void PetController::toggleAccessory(QString id)
{
    if (not wardrobe_ or not knownAccessory(id)) return;
    wardrobe_->toggle(id.toStdString());
    store_.setOutfit(outfitOf(*wardrobe_));
    emit frameChanged();
    emit outfitChanged();
}

void PetController::setEnabled(bool on)
{
    store_.setEnabled(on);
    if (window_) ::lens::app::pet::setPassthrough(*window_, enabled() and store_.passthrough());
    emit settingsChanged();
}

void PetController::setPassthrough(bool on)
{
    store_.setPassthrough(on);
    if (window_) ::lens::app::pet::setPassthrough(*window_, enabled() and on);
    emit settingsChanged();
}

void PetController::attachWindow(QObject* object)
{
    window_ = qobject_cast<QWindow*>(object);
    if (not window_) return;
    connect(window_, &QObject::destroyed, this, [this] { window_ = nullptr; });
    if (not excludeFromCapture(*window_)) LENS_WARN("the desktop pet can appear in screen captures: Windows refused the exclusion");
    ::lens::app::pet::setPassthrough(*window_, enabled() and store_.passthrough());
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &PetController::watchScreen);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &PetController::keepOnScreen);
    for (auto* screen : QGuiApplication::screens())
        watchScreen(screen);
}

void PetController::watchScreen(QScreen* screen)
{
    connect(screen, &QScreen::geometryChanged, this, &PetController::keepOnScreen);
    connect(screen, &QScreen::availableGeometryChanged, this, &PetController::keepOnScreen);
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, &PetController::keepOnScreen);
}

void PetController::keepOnScreen()
{
    if (not window_) return;
    auto areas = QList<QRect>{};
    for (auto const* screen : QGuiApplication::screens())
        areas.append(screen->availableGeometry());
    auto const at   = window_->position();
    auto const kept = placeWithin(at, window_->size(), areas, QGuiApplication::primaryScreen()->availableGeometry());
    if (kept != at) window_->setPosition(kept);
}

QPoint PetController::cursorPos() const
{
    return QCursor::pos();
}

QPoint PetController::initialPosition(QSize size) const
{
    auto const saved = store_.position();
    if (saved) {
        for (auto const* screen : QGuiApplication::screens()) {
            if (deviceNameOf(*screen).toStdString() != saved->screen) continue;
            auto const area = screen->availableGeometry();
            auto const at   = area.topLeft() + QPoint{saved->x, saved->y};
            if (fitsWithin(at, size, area)) return at;
            break;
        }
    }
    return bottomRightOf(size, QGuiApplication::primaryScreen()->availableGeometry());
}

void PetController::savePosition(QPoint at)
{
    auto const* screen = QGuiApplication::screenAt(at);
    if (not screen) return;
    auto const device = deviceNameOf(*screen);
    if (device.isEmpty()) return;
    auto const offset = at - screen->availableGeometry().topLeft();
    store_.setPosition(SavedPosition{device.toStdString(), offset.x(), offset.y()});
}

void PetController::tick()
{
    machine_->advance(kTick);
    sync(kTick);
}

void PetController::sync(std::chrono::milliseconds step)
{
    auto const& assets = *assets_;
    auto const action  = shownAction();
    if (action != shownAction_) {
        shownAction_   = action;
        actionElapsed_ = std::chrono::milliseconds{0};
    } else {
        actionElapsed_ += step;
    }

    // The blink runs on the expression layer while the action allows it: a wait, then a short run of expressions.
    auto const& blink      = assets.animations.blink;
    auto const blinkLength = std::chrono::milliseconds{blink.frames * 1000 / blink.fps};
    if (not assets.animations.actions.at(action).blinks) {
        blinkRunning_ = false;
        blinkWait_    = kBlinkEvery;
    } else if (blinkRunning_) {
        blinkElapsed_ += step;
        if (blinkElapsed_ >= blinkLength) {
            blinkRunning_ = false;
            blinkWait_    = kBlinkEvery;
        }
    } else {
        blinkWait_ -= step;
        if (blinkWait_ <= std::chrono::milliseconds{0}) {
            blinkRunning_ = true;
            blinkElapsed_ = std::chrono::milliseconds{0};
        }
    }

    auto const& spec     = assets.animations.actions.at(action);
    auto const& pictures = assets.sheets.at(action);
    auto frame           = static_cast<int>(actionElapsed_.count() * spec.fps / 1000);
    if (preview_ and not spec.loop and frame >= spec.frames) {
        // A one-shot preview has played through: the dog goes back to what the machine has it doing.
        preview_.reset();
        sync(std::chrono::milliseconds{0});
        return;
    }
    frame = spec.loop ? frame % spec.frames : std::min(frame, spec.frames - 1);

    auto expression = 0;
    if (blinkRunning_) {
        auto const& cells = assets.blinkExpressions;
        auto const cell   = static_cast<std::size_t>(blinkElapsed_.count() * blink.fps / 1000);
        if (not cells.empty()) expression = cells[std::min(cell, cells.size() - 1)];
    } else if (static_cast<std::size_t>(frame) < pictures.expressions.size()) {
        expression = pictures.expressions[static_cast<std::size_t>(frame)];
    }

    auto const name    = QString::fromStdString(actionName(action));
    auto const changed = name != action_ or frame != frameIndex_ or expression != expression_;
    if (name != action_) {
        sheets_ = QVariantMap{{"body", urlOf(pictures.body)}, {"effect", urlOf(pictures.effect)}, {"expression", urlOf(assets.expressionSheet)}};
    }
    action_     = name;
    frameIndex_ = frame;
    expression_ = expression;
    anchors_    = assets.anchors.at(action).at(static_cast<std::size_t>(frame));
    if (changed) emit frameChanged();
}

void PetController::updateClock()
{
    if (not machine_) return;
    if (desktopRunning_ or previewing_) {
        clock_.start();
    } else {
        clock_.stop();
    }
}

core::pet::Action PetController::shownAction() const
{
    return preview_.value_or(machine_->action());
}

std::optional<core::pet::Action> PetController::actionNamed(QString const& name) const
{
    if (not assets_) return std::nullopt;
    for (auto const& entry : assets_->animations.actions) {
        if (QString::fromStdString(actionName(entry.first)) == name) return entry.first;
    }
    return std::nullopt;
}

bool PetController::knownAccessory(QString const& id) const
{
    if (not assets_) return false;
    auto const name       = id.toStdString();
    auto const& catalogue = assets_->catalogue;
    return std::any_of(catalogue.begin(), catalogue.end(), [&name](auto const& spec) { return spec.id == name; });
}

} // namespace lens::app::pet
