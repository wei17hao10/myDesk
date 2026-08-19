/*
 * myDesk -- keyboard and mouse sharing utility
 * SPDX-FileCopyrightText: (C) 2026 myDesk Devs
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MonitorGraphicsItem.h"

#include "ScreenCanvasScene.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

MonitorGraphicsItem::MonitorGraphicsItem(
    ScreenCanvasScene *ownerScene, const QString &screenName, int monitorIndex, const QRectF &sceneRect,
    const QString &label
)
    : m_ownerScene(ownerScene),
      m_screenName(screenName),
      m_monitorIndex(monitorIndex),
      m_localRect(0, 0, sceneRect.width(), sceneRect.height()),
      m_label(label)
{
  setPos(sceneRect.topLeft());
  setFlag(ItemIsSelectable, true);
  setAcceptedMouseButtons(Qt::LeftButton);
}

QRectF MonitorGraphicsItem::boundingRect() const
{
  return m_localRect;
}

void MonitorGraphicsItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
  const QColor border = isSelected() ? QColor(66, 133, 244) : QColor(120, 120, 120);
  painter->setPen(QPen(border, isSelected() ? 3 : 2));
  painter->setBrush(m_ownerScene->colorForScreen(m_screenName));
  painter->drawRect(m_localRect);

  QString text = m_screenName;
  if (!m_label.isEmpty()) {
    text += "\n" + m_label;
  }

  // Draw the label in device pixels (reset the world transform) so it stays
  // legible at a fixed point size no matter how far the view is zoomed out —
  // the canvas can span a wide range of scales once machines/monitors of
  // very different physical sizes are arranged next to each other.
  const QRectF deviceRect = painter->worldTransform().mapRect(m_localRect);
  painter->save();
  painter->setWorldTransform(QTransform());
  QFont font = painter->font();
  font.setPointSizeF(13);
  font.setBold(true);
  painter->setFont(font);
  painter->setPen(Qt::black);
  painter->drawText(deviceRect, Qt::AlignCenter | Qt::TextWordWrap, text);
  painter->restore();
}

void MonitorGraphicsItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
  setSelected(true);
  m_dragStartScenePos = event->scenePos();
  m_dragStartPositions.clear();

  const auto movers = m_locked ? m_ownerScene->itemsForScreen(m_screenName) : QList<MonitorGraphicsItem *>{this};
  for (auto *item : movers) {
    m_dragStartPositions.insert(item, item->pos());
  }
  event->accept();
}

void MonitorGraphicsItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
  if (!m_dragStartPositions.contains(this)) {
    event->ignore();
    return;
  }

  const QPointF rawDelta = event->scenePos() - m_dragStartScenePos;
  const QPointF candidateTopLeft = m_dragStartPositions.value(this) + rawDelta;
  const QRectF candidateRect(candidateTopLeft, m_localRect.size());

  const auto movers = m_dragStartPositions.keys();
  const QPointF snappedTarget = m_ownerScene->snappedTopLeft(candidateRect, movers);
  const QPointF snappedDelta = snappedTarget - m_dragStartPositions.value(this);
  const QPointF candidateDelta = candidateTopLeft - m_dragStartPositions.value(this);

  // Never let a drag land on top of another machine's monitor — check EVERY
  // monitor being moved together (a locked whole-machine drag moves all of
  // that machine's monitors as a rigid group), not just the one directly
  // under the cursor.
  auto anyMoverOverlaps = [this, &movers](const QPointF &testDelta) {
    for (auto it = m_dragStartPositions.constBegin(); it != m_dragStartPositions.constEnd(); ++it) {
      MonitorGraphicsItem *mover = it.key();
      const QRectF resultRect(it.value() + testDelta, mover->m_localRect.size());
      if (m_ownerScene->wouldOverlapOtherMachine(mover->screenName(), resultRect, movers)) {
        return true;
      }
    }
    return false;
  };

  // Snapping to touch an edge is fine (that's how cross-machine links get
  // created); if snapping itself would create an overlap, fall back to the
  // raw (unsnapped) delta, and if even that overlaps, don't move at all.
  QPointF finalDelta = snappedDelta;
  if (anyMoverOverlaps(finalDelta)) {
    if (!anyMoverOverlaps(candidateDelta)) {
      finalDelta = candidateDelta;
    } else {
      event->accept();
      return;
    }
  }

  for (auto it = m_dragStartPositions.constBegin(); it != m_dragStartPositions.constEnd(); ++it) {
    it.key()->setPos(it.value() + finalDelta);
  }
  event->accept();
}

void MonitorGraphicsItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
  for (auto it = m_dragStartPositions.constBegin(); it != m_dragStartPositions.constEnd(); ++it) {
    auto *item = it.key();
    m_ownerScene->commitMonitorRect(item->screenName(), item->monitorIndex(), item->sceneRect());
  }
  m_dragStartPositions.clear();
  event->accept();
}
