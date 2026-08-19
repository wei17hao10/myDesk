/*
 * myDesk -- keyboard and mouse sharing utility
 * SPDX-FileCopyrightText: (C) 2026 myDesk Devs
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenCanvasScene.h"

#include "MonitorGraphicsItem.h"
#include "gui/config/CanvasTypes.h"

#include <cmath>

ScreenCanvasScene::ScreenCanvasScene(ScreenList &screens, QObject *parent) : QGraphicsScene(parent), m_Screens(screens)
{
  rebuildFromScreens();
}

void ScreenCanvasScene::rebuildFromScreens()
{
  clear();
  for (const auto &screen : m_Screens) {
    if (screen.isNull()) {
      continue;
    }
    const auto &monitors = screen.monitors();
    for (int i = 0; i < monitors.size(); ++i) {
      auto *item = new MonitorGraphicsItem(this, screen.name(), i, monitors[i].rect, monitors[i].label);
      item->setLocked(screen.isServer());
      addItem(item);
    }
  }
}

QColor ScreenCanvasScene::colorForScreen(const QString &name) const
{
  // Light, low-saturation palette — just enough to tell machines apart,
  // not attention-grabbing. Assigned by each non-null screen's position in
  // the list, so it stays stable as long as machines aren't reordered.
  static const QList<QColor> kPalette = {
      QColor(0xE3, 0xF2, 0xFD), // light blue
      QColor(0xE8, 0xF5, 0xE9), // light green
      QColor(0xFF, 0xF3, 0xE0), // light orange
      QColor(0xF3, 0xE5, 0xF5), // light purple
      QColor(0xFF, 0xEB, 0xEE), // light pink
      QColor(0xE0, 0xF2, 0xF1), // light teal
      QColor(0xFF, 0xFD, 0xE7), // light yellow
      QColor(0xEC, 0xEF, 0xF1), // light gray-blue
  };

  int index = 0;
  for (const auto &screen : m_Screens) {
    if (screen.isNull()) {
      continue;
    }
    if (screen.name() == name) {
      return kPalette[index % kPalette.size()];
    }
    ++index;
  }
  return kPalette.first();
}

Screen *ScreenCanvasScene::findScreen(const QString &name)
{
  for (auto &screen : m_Screens) {
    if (screen.name() == name) {
      return &screen;
    }
  }
  return nullptr;
}

QList<MonitorGraphicsItem *> ScreenCanvasScene::itemsForScreen(const QString &name) const
{
  QList<MonitorGraphicsItem *> result;
  for (auto *item : items()) {
    if (auto *monitorItem = dynamic_cast<MonitorGraphicsItem *>(item); monitorItem && monitorItem->screenName() == name) {
      result.append(monitorItem);
    }
  }
  return result;
}

void ScreenCanvasScene::addMachine(const Screen &newScreen)
{
  m_Screens.append(newScreen);
  rebuildFromScreens();
  Q_EMIT screensChanged();
}

void ScreenCanvasScene::removeMachine(const QString &screenName)
{
  for (int i = 0; i < m_Screens.size(); ++i) {
    if (m_Screens[i].name() == screenName) {
      m_Screens.removeAt(i);
      break;
    }
  }
  rebuildFromScreens();
  Q_EMIT screensChanged();
}

void ScreenCanvasScene::removeSelected()
{
  QStringList names;
  for (auto *item : selectedItems()) {
    if (auto *monitorItem = dynamic_cast<MonitorGraphicsItem *>(item); monitorItem && !names.contains(monitorItem->screenName())) {
      names.append(monitorItem->screenName());
    }
  }
  for (const auto &name : names) {
    removeMachine(name);
  }
}

QPointF ScreenCanvasScene::snappedTopLeft(const QRectF &candidateRect, const QList<MonitorGraphicsItem *> &excluding)
    const
{
  qreal snappedX = candidateRect.left();
  qreal snappedY = candidateRect.top();
  qreal bestDx = gui::canvas::kSnapTolerancePx;
  qreal bestDy = gui::canvas::kSnapTolerancePx;

  for (auto *item : items()) {
    auto *other = dynamic_cast<MonitorGraphicsItem *>(item);
    if (!other || excluding.contains(other)) {
      continue;
    }
    const QRectF otherRect = other->sceneRect();

    for (qreal edgeX : {otherRect.left(), otherRect.right()}) {
      for (qreal myEdge : {candidateRect.left(), candidateRect.right()}) {
        if (const qreal dx = edgeX - myEdge; std::abs(dx) < bestDx) {
          bestDx = std::abs(dx);
          snappedX = candidateRect.left() + dx;
        }
      }
    }
    for (qreal edgeY : {otherRect.top(), otherRect.bottom()}) {
      for (qreal myEdge : {candidateRect.top(), candidateRect.bottom()}) {
        if (const qreal dy = edgeY - myEdge; std::abs(dy) < bestDy) {
          bestDy = std::abs(dy);
          snappedY = candidateRect.top() + dy;
        }
      }
    }
  }

  return {snappedX, snappedY};
}

bool ScreenCanvasScene::wouldOverlapOtherMachine(
    const QString &screenName, const QRectF &rect, const QList<MonitorGraphicsItem *> &excluding
) const
{
  for (auto *item : items()) {
    auto *other = dynamic_cast<MonitorGraphicsItem *>(item);
    if (!other || excluding.contains(other) || other->screenName() == screenName) {
      continue;
    }
    if (other->sceneRect().intersects(rect)) {
      return true;
    }
  }
  return false;
}

void ScreenCanvasScene::commitMonitorRect(const QString &screenName, int monitorIndex, const QRectF &rect)
{
  Screen *screen = findScreen(screenName);
  if (!screen) {
    return;
  }
  auto monitors = screen->monitors();
  if (monitorIndex < 0 || monitorIndex >= monitors.size()) {
    return;
  }
  monitors[monitorIndex].rect = rect;
  screen->setMonitors(monitors);
  Q_EMIT screensChanged();
}
