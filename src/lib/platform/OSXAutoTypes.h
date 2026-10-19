/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2022 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <Carbon/Carbon.h>
#include <dispatch/dispatch.h>
#include <pthread.h>

#include <memory>
#include <type_traits>

using CFDeallocator = decltype(&CFRelease);
using AutoCFArray = std::unique_ptr<const __CFArray, CFDeallocator>;
using AutoCFDictionary = std::unique_ptr<const __CFDictionary, CFDeallocator>;
using AutoTISInputSourceRef = std::unique_ptr<__TISInputSource, CFDeallocator>;

// Text Input Source (TIS/TSM) calls abort the process if two threads are inside
// them at once. A mutex of our own isn't enough: AppKit (via Qt's
// QCocoaInputContext) calls TSM on the main thread whenever the user switches
// input source. So every TIS call runs on the main thread, serialized with
// AppKit's. Off the main thread this blocks until the main run loop executes f,
// so the main thread must never block waiting on the calling thread.
template <typename F> void runTis(F &&f)
{
  if (pthread_main_np()) {
    f();
    return;
  }
  dispatch_sync_f(dispatch_get_main_queue(), &f, [](void *ctx) { (*static_cast<std::remove_reference_t<F> *>(ctx))(); });
}
