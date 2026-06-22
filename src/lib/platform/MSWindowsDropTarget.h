/*
 * myDesk -- keyboard and mouse sharing utility
 * SPDX-FileCopyrightText: (C) 2026 myDesk Devs
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <ShlObj.h>
#include <objidl.h>
#include <windows.h>

#include <string>
#include <vector>

//! IDropTarget implementation for receiving file drops from Windows Explorer.
/*!
Register this with RegisterDragDrop() on the server's hidden window.
When the user drags a file across the screen edge (cursor leaves this screen),
Server::switchScreen() reads the pending files via getPendingFiles() and
sends them to the secondary machine.
*/
class MSWindowsDropTarget : public IDropTarget
{
public:
  MSWindowsDropTarget();
  ~MSWindowsDropTarget() override;

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  // IDropTarget
  STDMETHODIMP DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;
  STDMETHODIMP DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;
  STDMETHODIMP DragLeave() override;
  STDMETHODIMP Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) override;

  bool hasPendingFiles() const;
  std::vector<std::string> getPendingFiles() const;
  void clearPendingFiles();

private:
  static std::vector<std::string> extractFiles(IDataObject *pDataObj);

  LONG m_refCount = 1;
  // Files staged during DragEnter / DragOver; consumed by Server::switchScreen().
  std::vector<std::string> m_pendingFiles;
  bool m_isDragOver = false;
};
