// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/utils/memcopy.h"

#include "src/base/memcopy.h"
#include "src/snapshot/embedded/embedded-data-inl.h"

namespace v8::internal {

void InitMemCopyFunctions() {
#if defined(V8_TARGET_ARCH_IA32)
  if (Isolate::CurrentEmbeddedBlobIsBinaryEmbedded()) {
    EmbeddedData d = EmbeddedData::FromBlob();
    v8::base::g_memmove_function = reinterpret_cast<v8::base::MemMoveFunction>(
        d.InstructionStartOf(Builtin::kMemMove));
  }
#elif (V8_OS_POSIX || V8_OS_STARBOARD) && V8_HOST_ARCH_ARM
  if (Isolate::CurrentEmbeddedBlobIsBinaryEmbedded()) {
    EmbeddedData d = EmbeddedData::FromBlob();
    v8::base::g_memcopy_uint8_function =
        reinterpret_cast<v8::base::MemCopyUint8Function>(
            d.InstructionStartOf(Builtin::kMemCopyUint8Uint8));
  }
#endif  // (V8_OS_POSIX || V8_OS_STARBOARD) && V8_HOST_ARCH_ARM
}

}  // namespace v8::internal
