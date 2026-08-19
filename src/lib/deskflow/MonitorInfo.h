/*
 * myDesk -- keyboard and mouse sharing utility
 * SPDX-FileCopyrightText: (C) 2026 myDesk Devs
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>

// One physical monitor's position/size within a machine's combined virtual
// desktop (the same coordinate space IPlatformScreen::getShape() reports).
struct MonitorRect
{
  int32_t x = 0;
  int32_t y = 0;
  int32_t w = 0;
  int32_t h = 0;

  bool operator==(const MonitorRect &) const = default;
};
