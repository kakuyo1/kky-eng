/**
 * @file lens_pch.h
 * @brief Precompiled header for every target that links Qt Core.
 *
 * Qt's headers are re-parsed in every translation unit, and on MSVC in a Debug build that parse
 * is most of the compile. Precompiling them once per target turns ~90 repeats into one: measured
 * on this repository, the two targets that first took it went from 395 s to 218 s of total CPU,
 * and the QML ahead-of-time translation units -- one per .qml file -- from 152 s to 27 s.
 *
 * Only Qt Core and Qt Network are here, so a target that links no more than Core can take it.
 * The Quick-level headers live in lens_pch_quick.h, which needs modules not every target links.
 * Applied with target_precompile_headers(), so a target takes it only if its CMakeLists asks.
 */
#pragma once

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QMap>
#include <QtCore/QObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QVariant>
#include <QtCore/QVector>

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
