/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2013 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ClipboardTypes.h"

#include <string>
#include <string_view>

class IEventQueue;

class StreamChunker
{
public:
  static void sendClipboard(
      const std::string_view &data, size_t size, ClipboardID id, uint32_t sequence, IEventQueue *events,
      void *eventTarget
  );

  // Read filePath from disk and post FileSending events to eventTarget.
  // displayName overrides the on-wire filename (use relative path for folder contents).
  static void
  sendFile(const std::string &filePath, IEventQueue *events, void *eventTarget, const std::string &displayName = {});

  // Recursively transfer a folder: posts FolderStart, then sendFile() for every
  // file in the tree, then FolderEnd.
  static void sendFolder(const std::string &folderPath, IEventQueue *events, void *eventTarget);
};
