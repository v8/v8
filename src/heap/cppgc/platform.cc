// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/platform.h"

#include "src/base/platform/platform.h"
#include "src/heap/cppgc/gc-info-table.h"

namespace cppgc {
namespace internal {

static Platform* g_platform;

}  // namespace internal

void InitializePlatform(Platform* platform) {
  internal::g_platform = platform;
  internal::GlobalGCInfoTable::Create(internal::g_platform->GetPageAllocator());
}

Platform* GetPlatform() { return internal::g_platform; }

void ShutdownPlatform() { internal::g_platform = nullptr; }

namespace internal {

void Abort() { v8::base::OS::Abort(); }

}  // namespace internal
}  // namespace cppgc
