/*
 * myDesk -- keyboard and mouse sharing utility
 * SPDX-FileCopyrightText: (C) 2026 myDesk Devs
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QGraphicsView>
#include <QString>

class ScreenCanvasScene;

class ScreenCanvasView : public QGraphicsView
{
  Q_OBJECT

public:
  explicit ScreenCanvasView(QWidget *parent = nullptr);

  ScreenCanvasScene *canvasScene() const;
  void fitContents();

protected:
  void resizeEvent(QResizeEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void contextMenuEvent(QContextMenuEvent *event) override;

private:
  void showScreenConfig(const QString &screenName);
};
