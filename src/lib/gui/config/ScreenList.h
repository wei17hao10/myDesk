/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2021 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "Screen.h"

class ScreenList : public QList<Screen>
{
public:
  ScreenList() = default;
};
