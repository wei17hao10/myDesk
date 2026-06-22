/*
 * myDesk -- keyboard and mouse sharing utility
 * SPDX-FileCopyrightText: (C) 2026 myDesk Devs
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileChunk.h"

#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"

#include <cstring>

size_t FileChunk::s_expectedSize = 0;
std::string FileChunk::s_filename;

FileChunk::FileChunk(size_t size) : Chunk(size)
{
  // m_dataSize is the payload size (excluding the mark byte we handle inline)
  m_dataSize = size;
}

FileChunk *FileChunk::start(const std::string &filename, const std::string &filesize)
{
  // Payload: filename + '\0' + filesize_string
  std::string payload = filename + '\0' + filesize;
  auto *chunk = new FileChunk(payload.size());
  std::memcpy(chunk->m_chunk, payload.c_str(), payload.size());
  chunk->m_chunk[0] = ChunkType::DataStart; // overwrite first byte with mark
  // Re-encode properly: we store mark separately via send()
  // Reset and store full payload for send()
  delete chunk;

  // Simpler layout: m_chunk[0] = mark, rest = payload
  size_t total = 1 + payload.size();
  chunk = new FileChunk(total);
  chunk->m_chunk[0] = ChunkType::DataStart;
  std::memcpy(&chunk->m_chunk[1], payload.c_str(), payload.size());
  chunk->m_dataSize = total;
  return chunk;
}

FileChunk *FileChunk::data(const std::string &chunkData)
{
  size_t total = 1 + chunkData.size();
  auto *chunk = new FileChunk(total);
  chunk->m_chunk[0] = ChunkType::DataChunk;
  std::memcpy(&chunk->m_chunk[1], chunkData.c_str(), chunkData.size());
  chunk->m_dataSize = total;
  return chunk;
}

FileChunk *FileChunk::end()
{
  auto *chunk = new FileChunk(1);
  chunk->m_chunk[0] = ChunkType::DataEnd;
  chunk->m_dataSize = 1;
  return chunk;
}

TransferState FileChunk::assemble(deskflow::IStream *stream, std::string &dataCached, std::string &filename)
{
  using enum TransferState;
  uint8_t mark;
  std::string payload;

  if (!ProtocolUtil::readf(stream, kMsgDFileTransfer + 4, &mark, &payload)) {
    return Error;
  }

  if (mark == ChunkType::DataStart) {
    // payload = filename + '\0' + filesize_string
    auto sep = payload.find('\0');
    if (sep == std::string::npos) {
      LOG_ERR("file transfer start: malformed payload, missing separator");
      return Error;
    }
    s_filename = payload.substr(0, sep);
    filename = s_filename;
    std::string sizeStr = payload.substr(sep + 1);
    s_expectedSize = std::stoull(sizeStr);
    dataCached.clear();
    LOG_DEBUG("file transfer started: file=%s size=%zu", s_filename.c_str(), s_expectedSize);
    return Started;

  } else if (mark == ChunkType::DataChunk) {
    dataCached.append(payload);
    return InProgress;

  } else if (mark == ChunkType::DataEnd) {
    if (dataCached.size() != s_expectedSize) {
      LOG_ERR(
          "file transfer corrupt: expected=%zu actual=%zu", //
          s_expectedSize, dataCached.size()
      );
      return Error;
    }
    filename = s_filename;
    LOG_DEBUG("file transfer complete: file=%s size=%zu", s_filename.c_str(), dataCached.size());
    return Finished;
  }

  LOG_ERR("file transfer: unknown mark byte %d", mark);
  return Error;
}

void FileChunk::send(deskflow::IStream *stream, void *data)
{
  if (!stream || !data) {
    LOG_WARN("file transfer: stream or chunk is null, dropping chunk");
    return;
  }

  const auto *chunk = static_cast<FileChunk *>(data);

  uint8_t mark = static_cast<uint8_t>(chunk->m_chunk[0]);
  // payload is everything after the mark byte
  std::string payload(&chunk->m_chunk[1], chunk->m_dataSize - 1);

  ProtocolUtil::writef(stream, kMsgDFileTransfer, mark, &payload);

  switch (mark) {
  case ChunkType::DataStart:
    LOG_VERBOSE("sending file chunk start");
    break;
  case ChunkType::DataChunk:
    LOG_VERBOSE("sending file chunk data: size=%zu", payload.size());
    break;
  case ChunkType::DataEnd:
    LOG_VERBOSE("sending file chunk end");
    break;
  default:
    break;
  }
}
