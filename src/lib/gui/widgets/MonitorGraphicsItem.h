/*
 * myDesk -- keyboard and mouse sharing utility
 * SPDX-FileCopyrightText: (C) 2026 myDesk Devs
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QGraphicsObject>
#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QString>

class ScreenCanvasScene;

// One physical monitor on the arrangement canvas. Belongs to a machine
// (identified by screen name, looked up fresh on each mutation rather than
// held as a Screen* — ScreenList is a QList<Screen> and can reallocate).
class MonitorGraphicsItem : public QGraphicsObject
{
  Q_OBJECT

public:
  MonitorGraphicsItem(
      ScreenCanvasScene *ownerScene, const QString &screenName, int monitorIndex, const QRectF &sceneRect,
      const QString &label
  );

  QRectF boundingRect() const override;
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

  const QString &screenName() const
  {
    return m_screenName;
  }
  int monitorIndex() const
  {
    return m_monitorIndex;
  }
  QRectF sceneRect() const
  {
    return QRectF(pos(), m_localRect.size());
  }

  // Locked items can't be dragged individually; dragging any one of a
  // machine's locked monitors rigidly translates all of that machine's
  // monitors together (used for the local machine's real detected layout).
  void setLocked(bool locked)
  {
    m_locked = locked;
  }
  bool locked() const
  {
    return m_locked;
  }

protected:
  void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
  void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
  void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
  ScreenCanvasScene *m_ownerScene;
  QString m_screenName;
  int m_monitorIndex;
  QRectF m_localRect; // at origin; scene rect is QRectF(pos(), m_localRect.size())
  QString m_label;
  bool m_locked = false;

  QPointF m_dragStartScenePos;
  QHash<MonitorGraphicsItem *, QPointF> m_dragStartPositions;
};
