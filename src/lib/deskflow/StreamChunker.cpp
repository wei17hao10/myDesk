/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2013 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/StreamChunker.h"

#include "base/Event.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/ClipboardChunk.h"
#include "deskflow/FileChunk.h"

#include <QFileInfo>

#include <fstream>
#include <string>

static const size_t g_chunkSize = 512 * 1024;     // 512 KB for clipboard
static const size_t g_fileChunkSize = 256 * 1024; // 256 KB per file chunk

void StreamChunker::sendClipboard(
    const std::string_view &data, size_t size, ClipboardID id, uint32_t sequence, IEventQueue *events, void *eventTarget
)
{
  // send first message (data size)
  std::string dataSize = QString::number(size).toStdString();
  ClipboardChunk *sizeMessage = ClipboardChunk::start(id, sequence, dataSize);

  events->addEvent(Event(EventTypes::ClipboardSending, eventTarget, sizeMessage));

  // send clipboard chunk with a fixed size
  size_t sentLength = 0;
  size_t chunkSize = g_chunkSize;

  while (true) {
    // make sure we don't read too much from the mock data.
    if (sentLength + chunkSize > size) {
      chunkSize = size - sentLength;
    }

    std::string chunk(data.substr(sentLength, chunkSize).data(), chunkSize);
    ClipboardChunk *dataChunk = ClipboardChunk::data(id, sequence, chunk);

    events->addEvent(Event(EventTypes::ClipboardSending, eventTarget, dataChunk));

    sentLength += chunkSize;
    if (sentLength == size) {
      break;
    }
  }

  // send last message
  ClipboardChunk *end = ClipboardChunk::end(id, sequence);

  events->addEvent(Event(EventTypes::ClipboardSending, eventTarget, end));

  LOG_DEBUG("sent clipboard size=%d", sentLength);
}

void StreamChunker::sendFile(const std::string &filePath, IEventQueue *events, void *eventTarget)
{
  std::ifstream file(filePath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    LOG_WARN("file transfer: can't open file: %s", filePath.c_str());
    return;
  }

  const size_t fileSize = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);

  const std::string filename = QFileInfo(QString::fromStdString(filePath)).fileName().toStdString();

  if (fileSize == 0) {
    LOG_WARN("file transfer: skipping empty file: %s", filename.c_str());
    return;
  }

  LOG_INFO("file transfer: sending '%s' (%zu bytes)", filename.c_str(), fileSize);

  // DataStart — announce filename + size
  events->addEvent(Event(
      EventTypes::FileSending, eventTarget, //
      FileChunk::start(filename, std::to_string(fileSize))
  ));

  // DataChunk — send file content in pieces
  size_t sent = 0;
  while (sent < fileSize) {
    size_t remaining = fileSize - sent;
    size_t chunkSize = (remaining < g_fileChunkSize) ? remaining : g_fileChunkSize;

    std::string chunk(chunkSize, '\0');
    file.read(chunk.data(), static_cast<std::streamsize>(chunkSize));
    if (!file) {
      LOG_ERR("file transfer: read error after %zu bytes: %s", sent, filename.c_str());
      return;
    }

    events->addEvent(Event(EventTypes::FileSending, eventTarget, FileChunk::data(chunk)));
    sent += chunkSize;
  }

  // DataEnd
  events->addEvent(Event(EventTypes::FileSending, eventTarget, FileChunk::end()));

  LOG_DEBUG("file transfer: queued %zu bytes for '%s'", fileSize, filename.c_str());
}
