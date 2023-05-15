// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/default-thread-isolated-allocator.h"

#if V8_HAS_PKU_JIT_WRITE_PROTECT

#if !V8_OS_LINUX
#error pkey support in this file is only implemented on Linux
#endif

#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {

#if V8_HAS_PKU_JIT_WRITE_PROTECT

int PkeyAlloc() {
#ifdef SYS_pkey_alloc
  return static_cast<int>(syscall(SYS_pkey_alloc, 0, PKEY_DISABLE_WRITE));
#else
  return -1;
#endif
}
#endif

}  // namespace

namespace v8::platform {

DefaultThreadIsolatedAllocator::DefaultThreadIsolatedAllocator()
#if V8_HAS_PKU_JIT_WRITE_PROTECT
    : pkey_(PkeyAlloc())
#endif
{
}

DefaultThreadIsolatedAllocator::~DefaultThreadIsolatedAllocator() = default;

// TODO(sroettger): this should return thread isolated (e.g. pkey-tagged) memory
//                  for testing.
void* DefaultThreadIsolatedAllocator::Allocate(size_t size) {
  return malloc(size);
}

void DefaultThreadIsolatedAllocator::Free(void* object) { free(object); }

enum DefaultThreadIsolatedAllocator::Type DefaultThreadIsolatedAllocator::Type()
    const {
#if V8_HAS_PKU_JIT_WRITE_PROTECT
  return Type::kPkey;
#else
  UNREACHABLE();
#endif
}

int DefaultThreadIsolatedAllocator::Pkey() const {
#if V8_HAS_PKU_JIT_WRITE_PROTECT
  return pkey_;
#else
  UNREACHABLE();
#endif
}

bool DefaultThreadIsolatedAllocator::Valid() const {
#if V8_HAS_PKU_JIT_WRITE_PROTECT
  return pkey_ != -1;
#else
  return false;
#endif
}

}  // namespace v8::platform
