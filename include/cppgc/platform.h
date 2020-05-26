// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_PLATFORM_H_
#define INCLUDE_CPPGC_PLATFORM_H_

#include "v8-platform.h"  // NOLINT(build/include_directory)
#include "v8config.h"     // NOLINT(build/include_directory)

namespace cppgc {

// TODO(v8:10346): Put PageAllocator and Platform in a non-V8 include header to
// avoid depending on namespace v8.
using PageAllocator = v8::PageAllocator;
using Platform = v8::Platform;

// Initializes the garbage collector with the provided platform. Must be called
// before creating a Heap.
V8_EXPORT void InitializePlatform(Platform* platform);

V8_EXPORT Platform* GetPlatform();

// Must be called after destroying the last used heap.
V8_EXPORT void ShutdownPlatform();

namespace internal {

V8_EXPORT void Abort();

}  // namespace internal
}  // namespace cppgc

#endif  // INCLUDE_CPPGC_PLATFORM_H_
