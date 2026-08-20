/*
 * myDesk -- keyboard and mouse sharing utility
 * SPDX-FileCopyrightText: (C) 2026 myDesk Devs
 * SPDX-FileCopyrightText: (C) 2013 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_5.h"

#include "base/Event.h"
#include "base/EventTypes.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/FileChunk.h"
#include "deskflow/FileTransferStaging.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"
#include "server/Server.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

using deskflow::fileTransferStagingDir;
using deskflow::kFileTransferStagingMaxAgeMs;
using deskflow::purgeStaleFileTransfers;

ClientProxy1_5::ClientProxy1_5(const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events)
    : ClientProxy1_4(name, stream, server, events),
      m_events(events)
{
  purgeStaleFileTransfers(fileTransferStagingDir(), kFileTransferStagingMaxAgeMs);

  // Register FileSending event handler: when StreamChunker::sendFile() posts chunks,
  // this handler writes them to the remote client's stream.
  m_events->addHandler(EventTypes::FileSending, this, [this](const auto &e) {
    FileChunk::send(getStream(), e.getDataObject());
  });
}

ClientProxy1_5::~ClientProxy1_5()
{
  m_events->removeHandlers(this);
}

void ClientProxy1_5::sendDragInfo(uint32_t fileCount, const char *info, size_t size)
{
  std::string infoStr(info, size);
  LOG_DEBUG("sending drag info: %u file(s)", fileCount);
  ProtocolUtil::writef(getStream(), kMsgDDragInfo, fileCount, &infoStr);
}

void ClientProxy1_5::fileChunkSending(uint8_t mark, char *data, size_t dataSize)
{
  std::string dataStr(data, dataSize);
  ProtocolUtil::writef(getStream(), kMsgDFileTransfer, mark, &dataStr);
}

bool ClientProxy1_5::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgDFileTransfer, 4) == 0) {
    fileChunkReceived();
  } else if (memcmp(code, kMsgDDragInfo, 4) == 0) {
    dragInfoReceived();
  } else if (memcmp(code, kMsgDMonitorInfo, 4) == 0) {
    monitorInfoReceived();
  } else {
    return ClientProxy1_4::parseMessage(code);
  }
  return true;
}

void ClientProxy1_5::fileChunkReceived()
{
  auto state = FileChunk::assemble(getStream(), m_fileDataCached, m_transferFilename);
  switch (state) {
  case TransferState::Finished:
    saveReceivedFile(m_transferFilename, m_fileDataCached);
    m_fileDataCached.clear();
    m_transferFilename.clear();
    break;
  case TransferState::FolderStarted:
    // m_transferFilename holds the folder name from the FolderStart payload.
    beginFolderTransfer(m_transferFilename);
    m_transferFilename.clear();
    break;
  case TransferState::FolderFinished:
    completeFolderTransfer();
    break;
  case TransferState::Error:
    LOG_WARN("file transfer from client failed");
    m_fileDataCached.clear();
    m_transferFilename.clear();
    m_currentFolderName.clear();
    m_folderTargetPath.clear();
    break;
  default:
    break;
  }
}

void ClientProxy1_5::beginFolderTransfer(const std::string &folderName)
{
  const QString stagingDir = fileTransferStagingDir();
  purgeStaleFileTransfers(stagingDir, kFileTransferStagingMaxAgeMs);
  if (!QDir().mkpath(stagingDir)) {
    LOG_WARN("file transfer: could not create staging dir: %s", qPrintable(stagingDir));
    return;
  }
  const QDir dir(stagingDir);

  // Resolve name conflict for the top-level folder.
  QString targetPath = dir.filePath(QString::fromStdString(folderName));
  if (QFile::exists(targetPath)) {
    int n = 1;
    do {
      targetPath = dir.filePath(QStringLiteral("%1_%2").arg(QString::fromStdString(folderName)).arg(n++));
    } while (QFile::exists(targetPath));
  }

  if (!QDir().mkpath(targetPath)) {
    LOG_ERR("file transfer: could not create folder: %s", qPrintable(targetPath));
    return;
  }

  m_currentFolderName = folderName;
  m_folderTargetPath = targetPath;
  LOG_INFO("file transfer: receiving folder '%s' → '%s'", folderName.c_str(), qPrintable(targetPath));
}

void ClientProxy1_5::completeFolderTransfer()
{
  if (m_folderTargetPath.isEmpty()) {
    LOG_WARN("file transfer: folder end received but no active folder transfer");
    return;
  }
  LOG_INFO("file transfer: folder '%s' complete", m_currentFolderName.c_str());
  m_server->setClipboardFile(m_folderTargetPath.toStdString());
  m_events->addEvent(Event(EventTypes::FileReceived, m_server, static_cast<void *>(nullptr)));
  m_currentFolderName.clear();
  m_folderTargetPath.clear();
}

void ClientProxy1_5::dragInfoReceived()
{
  uint32_t fileCount = 0;
  std::string info;
  if (!ProtocolUtil::readf(getStream(), kMsgDDragInfo + 4, &fileCount, &info)) {
    LOG_WARN("failed to parse drag info from client");
    return;
  }
  m_pendingFileCount = static_cast<uint16_t>(fileCount);
  m_fileDataCached.clear();
  LOG_INFO("drag from client: %u file(s)", m_pendingFileCount);
}

void ClientProxy1_5::monitorInfoReceived()
{
  uint32_t monitorCount = 0;
  std::string info;
  if (!ProtocolUtil::readf(getStream(), kMsgDMonitorInfo + 4, &monitorCount, &info)) {
    LOG_WARN("failed to parse monitor info from client");
    return;
  }

  std::vector<MonitorRect> monitors;
  const QStringList rectStrings = QString::fromStdString(info).split(';', Qt::SkipEmptyParts);
  for (const QString &rectString : rectStrings) {
    const QStringList parts = rectString.split(',');
    if (parts.size() != 6) {
      continue;
    }
    monitors.push_back(
        {static_cast<int32_t>(parts[0].toInt()), static_cast<int32_t>(parts[1].toInt()),
         static_cast<int32_t>(parts[2].toInt()), static_cast<int32_t>(parts[3].toInt()),
         static_cast<int32_t>(parts[4].toInt()), static_cast<int32_t>(parts[5].toInt())}
    );
  }

  m_monitors = std::move(monitors);
  LOG_DEBUG("received monitor info from client: %zu monitor(s)", m_monitors.size());
  m_server->sendClientMonitorsIpc(this);
}

void ClientProxy1_5::saveReceivedFile(const std::string &filename, const std::string &data) const
{
  // --- Folder file: relative path like "FolderName/subdir/file.txt" ---
  if (!m_folderTargetPath.isEmpty()) {
    const auto sep = filename.find('/');
    if (sep == std::string::npos) {
      LOG_WARN("file transfer: folder file missing path separator: %s", filename.c_str());
      return;
    }
    const QString relPart = QString::fromStdString(filename.substr(sep + 1));

    // Reject path traversal attempts.
    for (const QString &component : relPart.split('/')) {
      if (component == ".." || component.isEmpty()) {
        LOG_WARN("file transfer: rejected unsafe path in folder transfer: %s", filename.c_str());
        return;
      }
    }

    const QString filePath = m_folderTargetPath + "/" + relPart;
    const QFileInfo fi(filePath);
    if (!fi.dir().exists() && !QDir().mkpath(fi.dir().absolutePath())) {
      LOG_ERR("file transfer: could not create subdir: %s", qPrintable(fi.dir().absolutePath()));
      return;
    }

    QFile out(filePath);
    if (!out.open(QIODevice::WriteOnly)) {
      LOG_ERR("file transfer: can't write to %s", qPrintable(filePath));
      return;
    }
    out.write(data.c_str(), static_cast<qint64>(data.size()));
    out.close();
    LOG_INFO("file transfer: saved '%s' (%zu bytes)", qPrintable(filePath), data.size());
    // Don't call setClipboardFile here — wait for FolderEnd (completeFolderTransfer).
    return;
  }

  // --- Single file transfer ---
  // Sanitize: strip any directory component to prevent path traversal.
  const QString safeBase = QFileInfo(QString::fromStdString(filename)).fileName();
  if (safeBase.isEmpty()) {
    LOG_WARN("file transfer: empty filename, discarding");
    return;
  }

  // Buffer files in a private temp subdirectory so they don't appear in the
  // user's Downloads folder. The user can then Ctrl+V / Cmd+V to paste to any folder.
  const QString stagingDir = fileTransferStagingDir();
  purgeStaleFileTransfers(stagingDir, kFileTransferStagingMaxAgeMs);
  if (!QDir().mkpath(stagingDir)) {
    LOG_WARN("file transfer: could not create staging dir: %s", qPrintable(stagingDir));
    return;
  }
  const QDir dir(stagingDir);

  // Avoid overwriting existing files.
  QString targetPath = dir.filePath(safeBase);
  if (QFile::exists(targetPath)) {
    const QFileInfo fi(safeBase);
    const QString base = fi.baseName();
    const QString ext = fi.suffix().isEmpty() ? QString() : "." + fi.suffix();
    int n = 1;
    do {
      targetPath = dir.filePath(QStringLiteral("%1_%2%3").arg(base).arg(n++).arg(ext));
    } while (QFile::exists(targetPath));
  }

  QFile out(targetPath);
  if (!out.open(QIODevice::WriteOnly)) {
    LOG_ERR("file transfer: can't write to %s", qPrintable(targetPath));
    return;
  }
  out.write(data.c_str(), static_cast<qint64>(data.size()));
  out.close();

  LOG_INFO(
      "file transfer: staged '%s' (%zu bytes) — ready for Ctrl+V/Cmd+V paste", qPrintable(targetPath), data.size()
  );
  m_server->setClipboardFile(targetPath.toStdString());

  // Notify the server so it can surface a GUI alert.
  m_events->addEvent(Event(EventTypes::FileReceived, m_server, static_cast<void *>(nullptr)));
}
