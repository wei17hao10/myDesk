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

ClientProxy1_5::ClientProxy1_5(
    const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events
)
    : ClientProxy1_4(name, stream, server, events)
    , m_events(events)
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
  } else {
    return ClientProxy1_4::parseMessage(code);
  }
  return true;
}

void ClientProxy1_5::fileChunkReceived()
{
  auto state = FileChunk::assemble(getStream(), m_fileDataCached, m_transferFilename);
  if (state == TransferState::Finished) {
    saveReceivedFile(m_transferFilename, m_fileDataCached);
    m_fileDataCached.clear();
    m_transferFilename.clear();
  } else if (state == TransferState::Error) {
    LOG_WARN("file transfer from client failed");
    m_fileDataCached.clear();
    m_transferFilename.clear();
  }
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

void ClientProxy1_5::saveReceivedFile(const std::string &filename, const std::string &data) const
{
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

  LOG_INFO("file transfer: staged '%s' (%zu bytes) — ready for Ctrl+V/Cmd+V paste", qPrintable(targetPath), data.size());
  m_server->setClipboardFile(targetPath.toStdString());

  // Notify the server so it can surface a GUI alert.
  m_events->addEvent(Event(EventTypes::FileReceived, m_server, static_cast<void *>(nullptr)));
}
