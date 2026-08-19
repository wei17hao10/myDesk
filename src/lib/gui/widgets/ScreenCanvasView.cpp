/*
 * myDesk -- keyboard and mouse sharing utility
 * SPDX-FileCopyrightText: (C) 2026 myDesk Devs
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenCanvasView.h"

#include "MonitorGraphicsItem.h"
#include "ScreenCanvasScene.h"
#include "dialogs/ScreenSettingsDialog.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QResizeEvent>

ScreenCanvasView::ScreenCanvasView(QWidget *parent) : QGraphicsView(parent)
{
  setRenderHint(QPainter::Antialiasing);
  setDragMode(NoDrag);
  setBackgroundBrush(QColor(245, 245, 245));
}

ScreenCanvasScene *ScreenCanvasView::canvasScene() const
{
  return qobject_cast<ScreenCanvasScene *>(scene());
}

void ScreenCanvasView::fitContents()
{
  if (auto *cs = canvasScene(); cs && !cs->itemsBoundingRect().isEmpty()) {
    fitInView(cs->itemsBoundingRect().marginsAdded(QMarginsF(40, 40, 40, 40)), Qt::KeepAspectRatio);
  }
}

void ScreenCanvasView::resizeEvent(QResizeEvent *event)
{
  QGraphicsView::resizeEvent(event);
  fitContents();
}

void ScreenCanvasView::mouseDoubleClickEvent(QMouseEvent *event)
{
  if (auto *item = dynamic_cast<MonitorGraphicsItem *>(itemAt(event->pos()))) {
    showScreenConfig(item->screenName());
  } else {
    QGraphicsView::mouseDoubleClickEvent(event);
  }
}

void ScreenCanvasView::contextMenuEvent(QContextMenuEvent *event)
{
  auto *item = dynamic_cast<MonitorGraphicsItem *>(itemAt(event->pos()));
  auto *cs = canvasScene();
  if (!item || !cs) {
    return;
  }

  QMenu menu(this);
  QAction *settingsAction = menu.addAction(tr("Computer Settings…"));
  QAction *lockAction = menu.addAction(item->locked() ? tr("Unlock Monitor Layout") : tr("Lock Monitor Layout"));
  menu.addSeparator();
  QAction *removeAction = menu.addAction(tr("Remove Computer"));

  QAction *chosen = menu.exec(event->globalPos());
  if (chosen == settingsAction) {
    showScreenConfig(item->screenName());
  } else if (chosen == lockAction) {
    const bool newLocked = !item->locked();
    for (auto *sibling : cs->itemsForScreen(item->screenName())) {
      sibling->setLocked(newLocked);
    }
  } else if (chosen == removeAction) {
    cs->removeMachine(item->screenName());
  }
}

void ScreenCanvasView::showScreenConfig(const QString &screenName)
{
  auto *cs = canvasScene();
  if (!cs) {
    return;
  }
  Screen *screen = cs->findScreen(screenName);
  if (!screen) {
    return;
  }
  ScreenSettingsDialog dlg(this, screen, &cs->screens());
  dlg.exec();
  Q_EMIT cs->screensChanged();
}
