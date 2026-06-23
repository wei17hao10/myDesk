/*
 * myDesk -- keyboard and mouse sharing utility
 * SPDX-FileCopyrightText: (C) 2026 myDesk Devs
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/Chunk.h"
#include "deskflow/ProtocolTypes.h"

#include <string>
#include <vector>

namespace deskflow {
class IStream;
}

class FileChunk : public Chunk
{
public:
  explicit FileChunk(size_t size);

  // DataStart payload: filename + '\0' + decimal filesize string
  static FileChunk *start(const std::string &filename, const std::string &filesize);
  static FileChunk *data(const std::string &chunkData);
  static FileChunk *end();

  // Folder transfer: FolderStart payload = folder name; FolderEnd has no payload.
  static FileChunk *folderStart(const std::string &folderName);
  static FileChunk *folderEnd();

  // Reassemble incoming kMsgDFileTransfer stream.
  // Returns Started / InProgress / Finished / Error.
  // On Finished, dataCached holds the complete file bytes and filename is set.
  static TransferState
  assemble(deskflow::IStream *stream, std::string &dataCached, std::string &filename);

  // Write a FileChunk to stream (used as event handler payload).
  static void send(deskflow::IStream *stream, void *data);

  static size_t getExpectedSize()
  {
    return s_expectedSize;
  }

  static const std::string &getFilename()
  {
    return s_filename;
  }

private:
  static size_t s_expectedSize;
  static std::string s_filename;
};
