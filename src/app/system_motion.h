#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

/**
 * @file system_motion.h
 * @brief Whether the reader has asked Windows to stop animating things.
 *
 * The interface is SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION): pvParam receives a BOOL
 * that is TRUE while client-area animations are on and FALSE once the reader has turned them
 * off. This is the Windows equivalent of the web's prefers-reduced-motion media query, and
 * there is no media query here -- a reader looking for one should stop at this file.
 *
 * Read once, in the constructor: the setting is a preference rather than a value that changes
 * while the app runs, and the surfaces bind to the answer. A call that fails leaves reduced
 * false, so an unreadable preference costs motion rather than removing it.
 *
 * @note Windows only, like the rest of src/app. This is where the Win32 call lives.
 */

namespace lens::app {

/**
 * @brief The QML singleton qml/theme/Motion.qml reads its durations from.
 *
 * The engine builds this one itself: it has no dependencies, so it needs none of the
 * provide()/create() dance AppController and Tray need, and QML_SINGLETON plus a default
 * constructor is the whole registration.
 */
class SystemMotion : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(SystemMotion)
    QML_SINGLETON
public:
    /// @brief Read the Windows animation preference, once.
    SystemMotion();

    /// @return True when the reader has turned Windows' animations off. Every duration in
    ///         qml/theme/Motion.qml then resolves to zero.
    bool reduced() const;

    Q_PROPERTY(bool reduced READ reduced CONSTANT)

private:
    bool reduced_ = false;
};

}
