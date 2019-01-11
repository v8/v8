// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/memcopy.h"

namespace v8 {
namespace internal {

#if V8_OS_POSIX && V8_HOST_ARCH_ARM
void MemCopyUint16Uint8Wrapper(uint16_t* dest, const uint8_t* src,
                               size_t chars) {
  uint16_t* limit = dest + chars;
  while (dest < limit) {
    *dest++ = static_cast<uint16_t>(*src++);
  }
}

V8_EXPORT_PRIVATE MemCopyUint8Function memcopy_uint8_function =
    &MemCopyUint8Wrapper;
MemCopyUint16Uint8Function memcopy_uint16_uint8_function =
    &MemCopyUint16Uint8Wrapper;
// Defined in codegen-arm.cc.
MemCopyUint8Function CreateMemCopyUint8Function(MemCopyUint8Function stub);
MemCopyUint16Uint8Function CreateMemCopyUint16Uint8Function(
    MemCopyUint16Uint8Function stub);

#elif V8_OS_POSIX && V8_HOST_ARCH_MIPS
V8_EXPORT_PRIVATE MemCopyUint8Function memcopy_uint8_function =
    &MemCopyUint8Wrapper;
// Defined in codegen-mips.cc.
MemCopyUint8Function CreateMemCopyUint8Function(MemCopyUint8Function stub);
#endif

static bool g_memcopy_functions_initialized = false;

void init_memcopy_functions() {
  if (g_memcopy_functions_initialized) return;
  g_memcopy_functions_initialized = true;
#if V8_OS_POSIX && V8_HOST_ARCH_ARM
  memcopy_uint8_function = CreateMemCopyUint8Function(&MemCopyUint8Wrapper);
  memcopy_uint16_uint8_function =
      CreateMemCopyUint16Uint8Function(&MemCopyUint16Uint8Wrapper);
#elif V8_OS_POSIX && V8_HOST_ARCH_MIPS
  memcopy_uint8_function = CreateMemCopyUint8Function(&MemCopyUint8Wrapper);
#endif
}

}  // namespace internal
}  // namespace v8
