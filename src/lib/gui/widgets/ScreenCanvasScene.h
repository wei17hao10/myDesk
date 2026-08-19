/*
 * myDesk -- keyboard and mouse sharing utility
 * SPDX-FileCopyrightText: (C) 2026 myDesk Devs
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/config/ScreenList.h"

#include <QColor>
#include <QGraphicsScene>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

class MonitorGraphicsItem;

// Free-form canvas: each configured machine (Screen) is drawn as one or more
// independently-draggable MonitorGraphicsItems. Operates directly on the
// ServerConfig's own ScreenList — no separate model/serialization layer.
class ScreenCanvasScene : public QGraphicsScene
{
  Q_OBJECT

public:
  explicit ScreenCanvasScene(ScreenList &screens, QObject *parent = nullptr);

  void rebuildFromScreens();
  void addMachine(const Screen &newScreen);
  void removeMachine(const QString &screenName);
  void removeSelected();

  ScreenList &screens()
  {
    return m_Screens;
  }
  const ScreenList &screens() const
  {
    return m_Screens;
  }
  Screen *findScreen(const QString &name);
  QList<MonitorGraphicsItem *> itemsForScreen(const QString &name) const;

  // A stable, light fill color for this machine's monitors, distinct from
  // other machines' — just enough to tell them apart at a glance.
  QColor colorForScreen(const QString &name) const;

  // Given a dragged item's candidate scene rect, returns a snapped top-left
  // if any other item's edge (excluding the ones currently being dragged
  // together) is within the snap tolerance.
  QPointF snappedTopLeft(const QRectF &candidateRect, const QList<MonitorGraphicsItem *> &excluding) const;

  // True if rect would overlap a monitor belonging to a DIFFERENT machine
  // than screenName (excluding items currently being dragged together).
  bool wouldOverlapOtherMachine(
      const QString &screenName, const QRectF &rect, const QList<MonitorGraphicsItem *> &excluding
  ) const;

  // Called by MonitorGraphicsItem when a drag finishes: persists the item's
  // current scene rect back into the owning Screen's monitor list.
  void commitMonitorRect(const QString &screenName, int monitorIndex, const QRectF &rect);

Q_SIGNALS:
  void screensChanged();

private:
  ScreenList &m_Screens;
};
