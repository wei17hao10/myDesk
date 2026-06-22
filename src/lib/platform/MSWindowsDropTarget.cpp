/*
 * myDesk -- keyboard and mouse sharing utility
 * SPDX-FileCopyrightText: (C) 2026 myDesk Devs
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsDropTarget.h"

#include "base/Log.h"

#include <ShlObj.h>
#include <shellapi.h>

MSWindowsDropTarget::MSWindowsDropTarget() = default;

MSWindowsDropTarget::~MSWindowsDropTarget() = default;

STDMETHODIMP MSWindowsDropTarget::QueryInterface(REFIID riid, void **ppv)
{
  if (riid == IID_IUnknown || riid == IID_IDropTarget) {
    *ppv = static_cast<IDropTarget *>(this);
    AddRef();
    return S_OK;
  }
  *ppv = nullptr;
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) MSWindowsDropTarget::AddRef()
{
  return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) MSWindowsDropTarget::Release()
{
  LONG count = InterlockedDecrement(&m_refCount);
  if (count == 0) {
    delete this;
  }
  return count;
}

STDMETHODIMP
MSWindowsDropTarget::DragEnter(IDataObject *pDataObj, DWORD /*grfKeyState*/, POINTL /*pt*/, DWORD *pdwEffect)
{
  m_isDragOver = false;
  m_pendingFiles = extractFiles(pDataObj);

  if (!m_pendingFiles.empty()) {
    LOG_DEBUG("MSWindowsDropTarget: DragEnter with %zu file(s)", m_pendingFiles.size());
    m_isDragOver = true;
    *pdwEffect = DROPEFFECT_COPY;
  } else {
    *pdwEffect = DROPEFFECT_NONE;
  }
  return S_OK;
}

STDMETHODIMP MSWindowsDropTarget::DragOver(DWORD /*grfKeyState*/, POINTL /*pt*/, DWORD *pdwEffect)
{
  *pdwEffect = m_isDragOver ? DROPEFFECT_COPY : DROPEFFECT_NONE;
  return S_OK;
}

STDMETHODIMP MSWindowsDropTarget::DragLeave()
{
  // Drag left the window — files remain staged for Server::switchScreen() to pick up.
  return S_OK;
}

STDMETHODIMP
MSWindowsDropTarget::Drop(IDataObject * /*pDataObj*/, DWORD /*grfKeyState*/, POINTL /*pt*/, DWORD *pdwEffect)
{
  // We don't actually let the drop land here; the file was already transferred
  // over the network when the cursor left the screen edge (switchScreen).
  *pdwEffect = DROPEFFECT_NONE;
  m_isDragOver = false;
  return S_OK;
}

bool MSWindowsDropTarget::hasPendingFiles() const
{
  return !m_pendingFiles.empty();
}

std::vector<std::string> MSWindowsDropTarget::getPendingFiles() const
{
  return m_pendingFiles;
}

void MSWindowsDropTarget::clearPendingFiles()
{
  m_pendingFiles.clear();
  m_isDragOver = false;
}

std::vector<std::string> MSWindowsDropTarget::extractFiles(IDataObject *pDataObj)
{
  std::vector<std::string> files;

  FORMATETC fmt = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM medium = {};

  if (FAILED(pDataObj->GetData(&fmt, &medium))) {
    return files;
  }

  HDROP hDrop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
  if (!hDrop) {
    ReleaseStgMedium(&medium);
    return files;
  }

  const UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
  for (UINT i = 0; i < count; ++i) {
    WCHAR path[MAX_PATH] = {};
    if (DragQueryFileW(hDrop, i, path, MAX_PATH) > 0) {
      // Convert wide string to UTF-8.
      int needed = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
      if (needed > 0) {
        std::string utf8(needed - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), needed, nullptr, nullptr);
        files.push_back(std::move(utf8));
      }
    }
  }

  GlobalUnlock(medium.hGlobal);
  ReleaseStgMedium(&medium);
  return files;
}
