/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2013 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "server/ClientProxy1_4.h"

#include <string>

class Server;
class IEventQueue;

//! Proxy for client implementing protocol version 1.5
class ClientProxy1_5 : public ClientProxy1_4
{
public:
  ClientProxy1_5(const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events);
  ClientProxy1_5(ClientProxy1_5 const &) = delete;
  ClientProxy1_5(ClientProxy1_5 &&) = delete;
  ~ClientProxy1_5() override;

  ClientProxy1_5 &operator=(ClientProxy1_5 const &) = delete;
  ClientProxy1_5 &operator=(ClientProxy1_5 &&) = delete;

  // Send drag metadata to the remote client (primary → secondary).
  void sendDragInfo(uint32_t fileCount, const char *info, size_t size) override;

  // Directly write one file chunk to the stream.
  // Used as a low-level fallback; prefer StreamChunker::sendFile() + event handler.
  void fileChunkSending(uint8_t mark, char *data, size_t dataSize) override;

  bool parseMessage(const uint8_t *code) override;

private:
  // Receive one kMsgDFileTransfer message from the connected client.
  void fileChunkReceived();
  // Receive one kMsgDDragInfo message from the connected client.
  void dragInfoReceived();

  // Saves a fully-received file to the local Downloads directory.
  void saveReceivedFile(const std::string &filename, const std::string &data) const;

  IEventQueue *m_events = nullptr;
  std::string m_fileDataCached;
  std::string m_transferFilename;
  uint16_t m_pendingFileCount = 0;
};
