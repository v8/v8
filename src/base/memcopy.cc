// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/memcopy.h"

#include <stdlib.h>
#include <string.h>

namespace v8::base {

#if defined(V8_HOST_ARCH_ARM)

namespace {
void MemCopyUint8Wrapper(uint8_t* dest, const uint8_t* src, size_t chars) {
  memcpy(dest, src, chars);
}
}  // namespace

MemCopyUint8Function g_memcopy_uint8_function = &MemCopyUint8Wrapper;

#endif  // defined(V8_HOST_ARCH_ARM)

}  // namespace v8::base
