// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_SANITIZERS_H_
#define V8_HEAP_CPPGC_SANITIZERS_H_

#include <stdint.h>
#include <string.h>

#include "src/base/macros.h"
#include "src/base/sanitizer/asan.h"
#include "src/base/sanitizer/msan.h"

// API for newly allocated or reclaimed memory.
#if defined(V8_USE_MEMORY_SANITIZER)
#define SET_MEMORY_ACCESSIBLE(address, size) \
  MSAN_MEMORY_IS_INITIALIZED(address, size);
#define SET_MEMORY_INACCESSIBLE(address, size) \
  memset((address), 0, (size));                \
  MSAN_ALLOCATED_UNINITIALIZED_MEMORY((address), (size))
#elif defined(V8_USE_ADDRESS_SANITIZER)
#define SET_MEMORY_ACCESSIBLE(address, size) \
  ASAN_UNPOISON_MEMORY_REGION(address, size);
#define SET_MEMORY_INACCESSIBLE(address, size) \
  memset((address), 0, (size));                \
  ASAN_POISON_MEMORY_REGION(address, size)
#elif DEBUG
#define SET_MEMORY_ACCESSIBLE(address, size) memset((address), 0, (size))
#define SET_MEMORY_INACCESSIBLE(address, size) \
  ::cppgc::internal::ZapMemory((address), (size));
#else
#define SET_MEMORY_ACCESSIBLE(address, size) ((void)(address), (void)(size))
#define SET_MEMORY_INACCESSIBLE(address, size) memset((address), 0, (size))
#endif

namespace cppgc {
namespace internal {

inline void ZapMemory(void* address, size_t size) {
  // The lowest bit of the zapped value should be 0 so that zapped object
  // are never viewed as fully constructed objects.
  static constexpr uint8_t kZappedValue = 0xdc;
  memset(address, kZappedValue, size);
}

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_SANITIZERS_H_
