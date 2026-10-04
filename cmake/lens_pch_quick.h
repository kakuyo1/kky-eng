/**
 * @file lens_pch_quick.h
 * @brief The Quick-level half of the precompiled header.
 *
 * Split from lens_pch.h because these modules are not linked everywhere: lens_llm and the
 * offline gtest targets stop at Qt Core, and a header they cannot resolve fails the compile.
 * A target lists both files when it links Qt Quick; CMake concatenates them into one PCH.
 */
#pragma once

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>

#include <QtWidgets/QMenu>
#include <QtWidgets/QSystemTrayIcon>

#include <QtQml/QJSEngine>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlListProperty>
#include <QtQml/qqmlprivate.h>

#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
