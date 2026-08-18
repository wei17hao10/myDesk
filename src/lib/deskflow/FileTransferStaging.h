/*
 * myDesk -- keyboard and mouse sharing utility
 * SPDX-FileCopyrightText: (C) 2026 myDesk Devs
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

namespace deskflow {

// Staged files are only meant to live until the user pastes them; anything
// older than this survived an app restart or a paste that never happened.
constexpr qint64 kFileTransferStagingMaxAgeMs = 24LL * 60 * 60 * 1000;

// Private temp subdirectory used to buffer received files so they don't land
// directly in a well-known folder (e.g. Downloads). The caller places the
// staged path on the clipboard so the user can Ctrl+V it to any folder.
QString fileTransferStagingDir();

// Remove staged entries under stagingDir older than maxAgeMs.
void purgeStaleFileTransfers(const QString &stagingDir, qint64 maxAgeMs);

} // namespace deskflow
