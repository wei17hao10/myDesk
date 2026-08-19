/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerConfig.h"

#include "Hotkey.h"
#include "common/Settings.h"

#include <QAbstractButton>
#include <QMap>
#include <QPair>
#include <QPushButton>

#include <algorithm>
#include <cmath>
#include <optional>

using enum ScreenConfig::Modifier;
using enum ScreenConfig::SwitchCorner;
using enum ScreenConfig::Fix;
using gui::canvas::Edge;
using gui::canvas::Interval;
using gui::canvas::kMinOverlapPx;

ServerConfig::ServerConfig()
{
  recall();
}

bool ServerConfig::save(const QString &fileName) const
{
  QFile file(fileName);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    return false;

  save(file);
  file.close();

  return true;
}

bool ServerConfig::operator==(const ServerConfig &sc) const
{
  return m_Screens == sc.m_Screens &&                                   //
         m_Heartbeat == sc.m_Heartbeat &&                               //
         m_RelativeMouseMoves == sc.m_RelativeMouseMoves &&             //
         m_Win32KeepForeground == sc.m_Win32KeepForeground &&           //
         m_SwitchDelay == sc.m_SwitchDelay &&                           //
         m_SwitchDoubleTap == sc.m_SwitchDoubleTap &&                   //
         m_SwitchCornerSize == sc.m_SwitchCornerSize &&                 //
         m_SwitchCorners == sc.m_SwitchCorners &&                       //
         m_Hotkeys == sc.m_Hotkeys &&                                   //
         m_DefaultLockToScreenState == sc.m_DefaultLockToScreenState && //
         m_DisableLockToScreen == sc.m_DisableLockToScreen &&           //
         m_ClipboardSharing == sc.m_ClipboardSharing &&                 //
         m_ClipboardSharingSize == sc.m_ClipboardSharingSize;
}

void ServerConfig::save(QFile &file) const
{
  QTextStream outStream(&file);
  outStream << *this;
}

void ServerConfig::setupScreens()
{
  switchCorners().clear();
  screens().clear();
  hotkeys().clear();

  // m_NumSwitchCorners is used as a fixed size array. See Screen::init()
  for (int i = 0; i < static_cast<int>(NumSwitchCorners); i++)
    switchCorners() << false;
}

void ServerConfig::commit()
{
  qDebug("committing server config");

  settings().beginGroup("internalConfig");
  settings().remove("");

  settings().setValue("heartbeat", heartbeat());
  settings().setValue("relativeMouseMoves", relativeMouseMoves());
  settings().setValue("win32KeepForeground", win32KeepForeground());
  settings().setValue("switchDelay", switchDelay());
  settings().setValue("switchDoubleTap", switchDoubleTap());
  settings().setValue("switchCornerSize", switchCornerSize());
  settings().setValue("defaultLockToScreenState", defaultLockToScreenState());
  settings().setValue("disableLockToScreen", disableLockToScreen());
  settings().setValue("clipboardSharing", clipboardSharing());
  settings().setValue("clipboardSharingSize", QVariant::fromValue(clipboardSharingSize()));

  writeSettings(settings(), switchCorners(), "switchCorner");

  settings().beginWriteArray("screens");
  for (int i = 0; i < screens().size(); i++) {
    settings().setArrayIndex(i);
    const auto &screen = screens()[i];
    screen.saveSettings(settings());
    auto screenName = Settings::value(Settings::Core::ComputerName).toString();
    if (screen.isServer() && screenName != screen.name()) {
      Settings::setValue(Settings::Core::ComputerName, screen.name());
    }
  }
  settings().endArray();

  settings().beginWriteArray("hotkeys");
  for (int i = 0; i < hotkeys().size(); i++) {
    settings().setArrayIndex(i);
    hotkeys()[i].saveSettings(settings().get());
  }
  settings().endArray();

  settings().endGroup();
}

void ServerConfig::recall()
{
  qDebug("recalling server config");

  settings().beginGroup("internalConfig");

  // Only used to migrate screens saved by the old fixed grid, which had no
  // canvas position of its own — see the migration loop below.
  const int legacyColumns = Settings::value(Settings::Server::GridWidth).toInt();

  setupScreens();

  setHeartbeat(settings().value("heartbeat", 5000).toInt());
  setRelativeMouseMoves(settings().value("relativeMouseMoves", false).toBool());
  setWin32KeepForeground(settings().value("win32KeepForeground", false).toBool());
  setSwitchDelay(settings().value("switchDelay", 250).toInt());
  setSwitchDoubleTap(settings().value("switchDoubleTap", 250).toInt());
  setSwitchCornerSize(settings().value("switchCornerSize").toInt());
  setDefaultLockToScreenState(settings().value("defaultLockToScreenState", false).toBool());
  setDisableLockToScreen(settings().value("disableLockToScreen", false).toBool());
  setClipboardSharingSize(
      settings().value("clipboardSharingSize", (int)ServerConfig::defaultClipboardSharingSize()).toULongLong()
  );
  setClipboardSharing(settings().value("clipboardSharing", true).toBool());

  readSettings(settings(), switchCorners(), "switchCorner", false, static_cast<int>(NumSwitchCorners));

  int numScreens = settings().beginReadArray("screens");
  for (int i = 0; i < numScreens; i++) {
    settings().setArrayIndex(i);
    Screen screen;
    screen.loadSettings(settings());
    if (screen.isNull()) {
      // Legacy fixed-grid configs padded unused cells with empty screens.
      continue;
    }
    if (getServerName() == screen.name()) {
      screen.markAsServer();
    }
    if (screen.monitors().isEmpty()) {
      // Migrating from the legacy fixed grid: synthesize a flush monitor
      // rect from this screen's old row/column slot so the canvas link
      // algorithm reproduces the exact same topology as the old grid writer,
      // until the user actually starts dragging things.
      constexpr qreal kLegacyCellW = 480.0;
      constexpr qreal kLegacyCellH = 320.0;
      const int row = legacyColumns > 0 ? i / legacyColumns : 0;
      const int col = legacyColumns > 0 ? i % legacyColumns : i;
      screen.setMonitors(
          {gui::canvas::MonitorRect{
              QRectF(col * kLegacyCellW, row * kLegacyCellH, kLegacyCellW, kLegacyCellH), QString()
          }}
      );
    }
    screens().append(screen);
  }
  settings().endArray();

  int numHotkeys = settings().beginReadArray("hotkeys");
  for (int i = 0; i < numHotkeys; i++) {
    settings().setArrayIndex(i);
    Hotkey h;
    h.loadSettings(settings().get());
    hotkeys().append(h);
  }
  settings().endArray();

  settings().endGroup();
}

namespace {

// Classifies how monitor rect a touches rect b (post-snap edges are exactly
// flush, so exact equality within a small epsilon is enough — no fuzzy-gap
// heuristic needed). Returns NoDirection-equivalent via std::nullopt if they
// don't touch.
std::optional<Edge> touchSide(const QRectF &a, const QRectF &b)
{
  constexpr qreal kEps = 0.5;
  if (std::abs(a.right() - b.left()) < kEps)
    return Edge::Right;
  if (std::abs(a.left() - b.right()) < kEps)
    return Edge::Left;
  if (std::abs(a.bottom() - b.top()) < kEps)
    return Edge::Bottom;
  if (std::abs(a.top() - b.bottom()) < kEps)
    return Edge::Top;
  return std::nullopt;
}

// Overlap of a and b along the axis perpendicular to side (the axis they
// actually share an edge on), or an empty range if they don't overlap.
std::pair<qreal, qreal> perpendicularOverlap(Edge side, const QRectF &a, const QRectF &b)
{
  if (side == Edge::Left || side == Edge::Right) {
    return {std::max(a.top(), b.top()), std::min(a.bottom(), b.bottom())};
  }
  return {std::max(a.left(), b.left()), std::min(a.right(), b.right())};
}

// Mirrors deskflow::server::Config::formatInterval(): whole-edge links
// ((0,1)) are written bare, partial ones as "(startPercent,endPercent)".
QString formatIntervalForConfig(const Interval &interval)
{
  if (interval.first == 0.0f && interval.second == 1.0f) {
    return {};
  }
  return QStringLiteral("(%1,%2)")
      .arg(static_cast<int>(std::lround(interval.first * 100.0)))
      .arg(static_cast<int>(std::lround(interval.second * 100.0)));
}

Interval normalizeToBBox(Edge side, qreal ov0, qreal ov1, const QRectF &bbox)
{
  qreal start;
  qreal end;
  if (side == Edge::Left || side == Edge::Right) {
    start = (ov0 - bbox.top()) / bbox.height();
    end = (ov1 - bbox.top()) / bbox.height();
  } else {
    start = (ov0 - bbox.left()) / bbox.width();
    end = (ov1 - bbox.left()) / bbox.width();
  }
  start = std::clamp(start, 0.0, 1.0);
  end = std::clamp(end, 0.0, 1.0);
  // Round to the nearest 1/100 to match Config::formatInterval's
  // integer-percent grammar.
  start = std::round(start * 100.0) / 100.0;
  end = std::round(end * 100.0) / 100.0;
  return {static_cast<float>(start), static_cast<float>(end)};
}

} // namespace

QList<ServerConfig::TouchingPair> ServerConfig::computeCanvasLinks() const
{
  QList<TouchingPair> pairs;

  for (int a = 0; a < screens().size(); ++a) {
    const Screen &screenA = screens()[a];
    if (screenA.isNull())
      continue;
    const QRectF bboxA = screenA.boundingRect();

    for (int b = a + 1; b < screens().size(); ++b) {
      const Screen &screenB = screens()[b];
      if (screenB.isNull())
        continue;
      const QRectF bboxB = screenB.boundingRect();

      for (const auto &monitorA : screenA.monitors()) {
        for (const auto &monitorB : screenB.monitors()) {
          const auto side = touchSide(monitorA.rect, monitorB.rect);
          if (!side)
            continue;

          const auto [ov0, ov1] = perpendicularOverlap(*side, monitorA.rect, monitorB.rect);
          if (ov1 - ov0 < kMinOverlapPx)
            continue;

          const Interval fracA = normalizeToBBox(*side, ov0, ov1, bboxA);
          const Interval fracB = normalizeToBBox(gui::canvas::oppositeEdge(*side), ov0, ov1, bboxB);
          if (fracA.first >= fracA.second || fracB.first >= fracB.second)
            continue;

          pairs.append(TouchingPair{screenA.name(), screenB.name(), *side, fracA, fracB});
        }
      }
    }
  }

  return pairs;
}

bool ServerConfig::validateCanvasLayout(QStringList &errors) const
{
  errors.clear();

  // Same-machine monitor overlap.
  for (const auto &screen : screens()) {
    if (screen.isNull())
      continue;
    const auto &monitors = screen.monitors();
    for (int i = 0; i < monitors.size(); ++i) {
      for (int j = i + 1; j < monitors.size(); ++j) {
        if (monitors[i].rect.intersects(monitors[j].rect)) {
          errors.append(QObject::tr("Two monitors of \"%1\" overlap.").arg(screen.name()));
        }
      }
    }
  }

  // Cross-machine monitor overlap (interactive dragging already prevents
  // this, but a locked group-move or a hand-edited config could still slip
  // one through).
  for (int a = 0; a < screens().size(); ++a) {
    const Screen &screenA = screens()[a];
    if (screenA.isNull())
      continue;
    for (int b = a + 1; b < screens().size(); ++b) {
      const Screen &screenB = screens()[b];
      if (screenB.isNull())
        continue;
      for (const auto &monitorA : screenA.monitors()) {
        for (const auto &monitorB : screenB.monitors()) {
          if (monitorA.rect.intersects(monitorB.rect)) {
            errors.append(
                QObject::tr("\"%1\" and \"%2\" have overlapping monitors.").arg(screenA.name(), screenB.name())
            );
          }
        }
      }
    }
  }

  // Per-(screen,side) interval collision check, mirroring what
  // deskflow::server::Config::connect() would otherwise reject at parse
  // time with a much less actionable error.
  QMap<QPair<QString, int>, QList<Interval>> intervalsBySide;
  for (const auto &pair : computeCanvasLinks()) {
    intervalsBySide[{pair.screenA, static_cast<int>(pair.sideOnA)}].append(pair.intervalOnA);
    intervalsBySide[{pair.screenB, static_cast<int>(gui::canvas::oppositeEdge(pair.sideOnA))}].append(
        pair.intervalOnB
    );
  }
  for (auto it = intervalsBySide.constBegin(); it != intervalsBySide.constEnd(); ++it) {
    auto sorted = it.value();
    std::sort(sorted.begin(), sorted.end());
    for (int i = 1; i < sorted.size(); ++i) {
      if (sorted[i].first < sorted[i - 1].second) {
        errors.append(QObject::tr("\"%1\" has overlapping monitor links on one edge.").arg(it.key().first));
      }
    }
  }

  return errors.isEmpty();
}

QTextStream &operator<<(QTextStream &outStream, const ServerConfig &config)
{
  outStream << "section: screens" << Qt::endl;

  for (const Screen &s : config.screens()) {
    if (!s.isNull())
      outStream << s.screensSection();
  }

  outStream << "end" << Qt::endl << Qt::endl;

  outStream << "section: aliases" << Qt::endl;

  for (const Screen &s : config.screens()) {
    if (!s.isNull())
      outStream << s.aliasesSection();
  }

  outStream << "end" << Qt::endl << Qt::endl;

  outStream << "section: links" << Qt::endl;

  QMap<QString, QStringList> linkLinesByScreen;
  for (const auto &pair : config.computeCanvasLinks()) {
    const auto oppositeSide = gui::canvas::oppositeEdge(pair.sideOnA);
    linkLinesByScreen[pair.screenA].append(QStringLiteral("\t\t%1%2 = %3%4")
                                                .arg(gui::canvas::edgeConfigName(pair.sideOnA))
                                                .arg(formatIntervalForConfig(pair.intervalOnA))
                                                .arg(pair.screenB)
                                                .arg(formatIntervalForConfig(pair.intervalOnB)));
    linkLinesByScreen[pair.screenB].append(QStringLiteral("\t\t%1%2 = %3%4")
                                                .arg(gui::canvas::edgeConfigName(oppositeSide))
                                                .arg(formatIntervalForConfig(pair.intervalOnB))
                                                .arg(pair.screenA)
                                                .arg(formatIntervalForConfig(pair.intervalOnA)));
  }

  for (const Screen &s : config.screens()) {
    if (!s.isNull()) {
      outStream << "\t" << s.name() << ":\n";
      for (const auto &line : linkLinesByScreen.value(s.name())) {
        outStream << line << Qt::endl;
      }
    }
  }

  outStream << "end" << Qt::endl << Qt::endl;

  outStream << "section: options" << Qt::endl;

  if (Settings::value(Settings::Server::EnableHeatbeat).toBool())
    outStream << "\t" << "heartbeat = " << config.heartbeat() << Qt::endl;

  outStream << "\t"
            << "relativeMouseMoves = " << (config.relativeMouseMoves() ? "true" : "false") << Qt::endl;
  outStream << "\t"
            << "win32KeepForeground = " << (config.win32KeepForeground() ? "true" : "false") << Qt::endl;
  outStream << "\t"
            << "defaultLockToScreenState = " << (config.defaultLockToScreenState() ? "true" : "false") << Qt::endl;
  outStream << "\t"
            << "disableLockToScreen = " << (config.disableLockToScreen() ? "true" : "false") << Qt::endl;
  outStream << "\t"
            << "clipboardSharing = " << (config.clipboardSharing() ? "true" : "false") << Qt::endl;
  outStream << "\t"
            << "clipboardSharingSize = " << config.clipboardSharingSize() << Qt::endl;

  if (Settings::value(Settings::Server::EnableSwitchDelay).toBool())
    outStream << "\t"
              << "switchDelay = " << config.switchDelay() << Qt::endl;

  if (Settings::value(Settings::Server::EnableSwitchDoubleTap).toBool())
    outStream << "\t"
              << "switchDoubleTap = " << config.switchDoubleTap() << Qt::endl;

  outStream << "\t"
            << "switchCorners = none ";
  for (int i = 0; i < config.switchCorners().size(); i++)
    if (config.switchCorners()[i])
      outStream << "+" << ServerConfig::switchCornerName(i) << " ";
  outStream << Qt::endl;

  outStream << "\t"
            << "switchCornerSize = " << config.switchCornerSize() << Qt::endl;

  for (const Hotkey &hotkey : config.hotkeys())
    outStream << hotkey;

  outStream << "end" << Qt::endl << Qt::endl;

  return outStream;
}

int ServerConfig::numScreens() const
{
  int rval = 0;

  for (const Screen &s : screens()) {
    if (!s.isNull())
      rval++;
  }

  return rval;
}

QString ServerConfig::getServerName() const
{
  return Settings::value(Settings::Core::ComputerName).toString();
}

void ServerConfig::updateServerName()
{
  for (auto &screen : screens()) {
    if (screen.isServer()) {
      screen.setName(Settings::value(Settings::Core::ComputerName).toString());
      break;
    }
  }
}

QString ServerConfig::configFile() const
{
  return Settings::value(Settings::Server::ExternalConfigFile).toString();
}

bool ServerConfig::useExternalConfig() const
{
  return Settings::value(Settings::Server::ExternalConfig).toBool();
}

bool ServerConfig::screenExists(const QString &screenName) const
{
  bool isExists = false;

  for (const auto &screen : screens()) {
    if (!screen.isNull() && screen.name() == screenName) {
      isExists = true;
      break;
    }
  }

  return isExists;
}

void ServerConfig::setConfigFile(const QString &configFile) const
{
  Settings::setValue(Settings::Server::ExternalConfigFile, configFile);
}

void ServerConfig::setUseExternalConfig(bool useExternalConfig) const
{
  Settings::setValue(Settings::Server::ExternalConfig, useExternalConfig);
}

size_t ServerConfig::defaultClipboardSharingSize()
{
  return 3 * 1024; // 3 MiB
}

size_t ServerConfig::setClipboardSharingSize(size_t size)
{
  if (size) {
    size += 512; // Round up to the nearest megabyte
    size /= 1024;
    size *= 1024;
    setClipboardSharing(true);
  } else {
    setClipboardSharing(false);
  }
  using std::swap;
  swap(size, m_ClipboardSharingSize);
  return size;
}

QSettingsProxy &ServerConfig::settings()
{
  return Settings::proxy();
}
