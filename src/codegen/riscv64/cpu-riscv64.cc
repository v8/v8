// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// CPU specific code for arm independent of OS goes here.

#include <sys/syscall.h>
#include <unistd.h>

#if V8_TARGET_ARCH_RISCV64

#include "src/codegen/cpu-features.h"

namespace v8 {
namespace internal {

void CpuFeatures::FlushICache(void* start, size_t size) {
#if !defined(USE_SIMULATOR)
  __builtin___clear_cache(start, (char *)start + size);
#endif  // !USE_SIMULATOR.
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_RISCV64
