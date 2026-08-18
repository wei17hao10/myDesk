/*
 * myDesk -- keyboard and mouse sharing utility
 * SPDX-FileCopyrightText: (C) 2026 myDesk Devs
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileTransferStaging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace deskflow {

QString fileTransferStagingDir()
{
  return QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/myDesk-transfers";
}

void purgeStaleFileTransfers(const QString &stagingDir, qint64 maxAgeMs)
{
  const QDir dir(stagingDir);
  if (!dir.exists()) {
    return;
  }

  const QDateTime cutoff = QDateTime::currentDateTime().addMSecs(-maxAgeMs);
  for (const QFileInfo &entry : dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries)) {
    if (entry.lastModified() >= cutoff) {
      continue;
    }
    if (entry.isDir()) {
      QDir(entry.absoluteFilePath()).removeRecursively();
    } else {
      QFile::remove(entry.absoluteFilePath());
    }
  }
}

} // namespace deskflow
