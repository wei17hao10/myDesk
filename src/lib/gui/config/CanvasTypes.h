/*
 * myDesk -- keyboard and mouse sharing utility
 * SPDX-FileCopyrightText: (C) 2026 myDesk Devs
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QRect>
#include <QRectF>
#include <QString>

#include <cstdint>
#include <utility>

namespace gui::canvas {

// [0,1] sub-range of one edge, mirrors deskflow::server::Config::Interval.
// The gui library doesn't link server, so this is a small parallel type
// sharing only the on-disk text grammar, not the C++ type.
using Interval = std::pair<float, float>;

enum class Edge : uint8_t
{
  Left,
  Right,
  Top,
  Bottom
};

// Must match deskflow::server::Config::dirName()'s text grammar exactly
// (Config.cpp: "left","right","up","down").
inline const char *edgeConfigName(Edge e)
{
  switch (e) {
  case Edge::Left:
    return "left";
  case Edge::Right:
    return "right";
  case Edge::Top:
    return "up";
  case Edge::Bottom:
    return "down";
  }
  return "left";
}

inline Edge oppositeEdge(Edge e)
{
  switch (e) {
  case Edge::Left:
    return Edge::Right;
  case Edge::Right:
    return Edge::Left;
  case Edge::Top:
    return Edge::Bottom;
  case Edge::Bottom:
    return Edge::Top;
  }
  return e;
}

// One physical monitor, positioned in absolute canvas coordinates. A
// machine's bounding box is the union of its own monitors' rects.
//
// Each monitor is drawn at its own true physical size, so monitors of
// different pixel densities on one machine use different canvas scales.
// pixelRect is the monitor's real geometry in its machine's own OS
// coordinate space (what the server's crossing math uses); it lets canvas
// positions on each monitor be mapped back to pixels independently. An
// empty pixelRect (layouts saved before it existed) means the machine's
// monitors share one uniform scale and the canvas bbox maps linearly.
struct MonitorRect
{
  QRectF rect;
  QString label;
  QRect pixelRect;

  bool operator==(const MonitorRect &) const = default;
};

// Drag-snap capture radius, and the minimum edge overlap treated as a real
// touch rather than a corner graze (below this, no link is generated).
constexpr float kSnapTolerancePx = 12.0f;
constexpr float kMinOverlapPx = 4.0f;

// True only if a and b share a real area, not just an edge. QRectF::intersects()
// is exact, so two monitors that touch edge-to-edge can register as
// overlapping once translation/snapping arithmetic leaves the shared edge off
// by a rounding error (e.g. (x - w) + w != x in floating point).
inline bool rectsOverlap(const QRectF &a, const QRectF &b)
{
  constexpr qreal kTolerance = 0.5;
  const QRectF overlap = a.intersected(b);
  return overlap.width() > kTolerance && overlap.height() > kTolerance;
}

} // namespace gui::canvas
